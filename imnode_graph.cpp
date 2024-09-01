// =====================================================================================================================
// Copyright 2024 Medusa Slockbower
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// 	http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =====================================================================================================================

#include "imnode_graph.h"
#include "imnode_graph_internal.h"

#include <imgui-docking/imgui_internal.h>

#include <iostream>

//#define IMNODE_GRAPH_DEBUG_PIN_BOUNDS

struct ImNodeFontConfig
{
	char* Path;
	float Size;
	const ImWchar* GlyphRanges;
};

ImNodeGraphContext*   GImNodeGraph = nullptr;
ImVector<ImNodeFontConfig> GFonts;
float                GFontUpscale = 4.0f;

ImVec4 operator*(const ImVec4& v, float s) { return { v.x * s, v.y * s, v.z * s, v.w * s }; }

// =====================================================================================================================
// Internal Extensions
// =====================================================================================================================


bool ImGui::IsAnyModKeyDown()
{
	ImGuiContext& G = *GImGui;
	ImGuiIO&     IO = G.IO;

	return IO.KeyMods != ImGuiMod_None;
}

// =====================================================================================================================
// Internal Functionality
// =====================================================================================================================


// Math ----------------------------------------------------------------------------------------------------------------

bool ImAABB(const ImRect &a, const ImRect &b)
{
    return a.Max.x > b.Min.x
    &&     a.Min.x < b.Max.x
    &&     a.Max.y > b.Min.y
    &&     a.Min.y < b.Max.y;
}


// Context -------------------------------------------------------------------------------------------------------------

ImNodeGraphContext::ImNodeGraphContext()
	: Initialized(false)
	, Scope(ImNodeGraphScope_None)
	, CurrentGraph(nullptr)
{

}

void ImNodeGraph::Initialize()
{
	ImGuiContext&     Ctx = *ImGui::GetCurrentContext();
	ImNodeGraphContext& G = *GImNodeGraph;
	IM_ASSERT(!G.Initialized);

	// If no fonts were set up, add the default font
	if(G.Fonts.empty())
	{
		if(Ctx.IO.Fonts->Fonts.Size == 0) Ctx.IO.Fonts->AddFontDefault();

		LoadFonts();
	}

	G.Initialized = true;
}

void ImNodeGraph::Shutdown()
{
	ImNodeGraphContext& G = *GImNodeGraph;
	IM_ASSERT(G.Initialized);

	G.Graphs.clear_delete();
	GFonts.clear();
}

void ImNodeGraph::LoadFonts()
{
	if(GFonts.empty())
	{
		LoadDefaultFont();
		return;
	}

	ImGuiContext&     Ctx = *ImGui::GetCurrentContext();
	ImNodeGraphContext& G = *GImNodeGraph;

	bool first = true;
	for(const auto& font : GFonts)
	{
		ImFontConfig cfg = ImFontConfig();
		cfg.OversampleH = cfg.OversampleV = 1;
		cfg.SizePixels = font.Size * GFontUpscale;
		cfg.MergeMode  = !first;
		cfg.PixelSnapH = false;
		G.Fonts.push_back(Ctx.IO.Fonts->AddFontFromFileTTF(font.Path, 0, &cfg, font.GlyphRanges));

		first = false;
	}
}

void ImNodeGraph::LoadDefaultFont()
{
	ImGuiContext&     Ctx = *ImGui::GetCurrentContext();
	ImNodeGraphContext& G = *GImNodeGraph;

	ImFontConfig cfg = ImFontConfig();
	cfg.OversampleH = cfg.OversampleV = 1;
	cfg.SizePixels = 20.0f * GFontUpscale;
	cfg.MergeMode = false;
	cfg.PixelSnapH = true;
	G.Fonts.push_back(Ctx.IO.Fonts->AddFontDefault(&cfg));
}


// Graph ---------------------------------------------------------------------------------------------------------------

ImNodeGraphData::ImNodeGraphData(ImNodeGraphContext* ctx, const char* name)
	: Ctx(ctx)
	, Flags(ImNodeGraphFlags_None)
	, Name(nullptr)
	, ID(ImHashStr(name))
	, TargetZoom(1.0f)
	, IsPanning(false)
	, CurrentNode(nullptr)
	, CurrentPin(nullptr)
	, SubmitCount(0)
    , Validation(nullptr)
{ Name = ImStrdup(name); }

ImPinData& ImNodeGraphData::FindPin(ImPinPtr pin)
{
	ImNodeData&              Node = Nodes[pin.Node];
	ImObjectPool<ImPinData>& Pins = pin.Direction ? Node.OutputPins : Node.InputPins;
	return Pins[pin.Pin];
}

ImRect ImNodeGraphData::GetSelection()
{
    if(SelectRegionStart() == false) return { { -1, -1 }, { -1, -1 } };

    ImVec2 mouse = ImGui::GetMousePos();
    return { ImMin(mouse, SelectRegionStart), ImMax(mouse, SelectRegionStart) };
}

void ImNodeGraphData::UpdateSelection(ImGuiID Node, bool allow_clear, bool removal)
{
	ImGuiContext& Ctx = *ImGui::GetCurrentContext();
    ImGuiIO&       IO = Ctx.IO;
    bool     selected = Selected.Contains(Node);

    switch(IO.KeyMods)
    {
        case ImGuiMod_Ctrl:
            if(selected) Selected.Erase(Node);
            else         Selected.Insert(Node);
            break;
        default:
            if(allow_clear) Selected.Clear();
        case ImGuiMod_Shift:
            if(removal) Selected.Erase(Node);
            else        Selected.Insert(Node);
    }
}

ImNodeGraphData* ImNodeGraph::FindGraphByID(ImGuiID id)
{
	ImNodeGraphContext& G = *GImNodeGraph;
	return static_cast<ImNodeGraphData*>(G.GraphsById.GetVoidPtr(id));
}

ImNodeGraphData* ImNodeGraph::FindGraphByTitle(const char *title)
{
	ImGuiID id = ImHashStr(title);
	return FindGraphByID(id);
}

ImNodeGraphData* ImNodeGraph::CreateNewGraph(const char *title)
{
	ImNodeGraphContext& G = *GImNodeGraph;
	ImNodeGraphData* Graph = IM_NEW(ImNodeGraphData)(&G, title);
	G.GraphsById.SetVoidPtr(Graph->ID, Graph);

	G.Graphs.push_back(Graph);

	return Graph;
}

void ImNodeGraph::DrawGrid(const ImRect &grid_bounds)
{
	// Draw the grid
	ImGuiWindow& DrawWindow = *ImGui::GetCurrentWindow();
	ImDrawList&    DrawList = *DrawWindow.DrawList;
	ImNodeGraphData&  Graph = *GImNodeGraph->CurrentGraph;
	ImNodeGraphStyle& Style =  Graph.Style;
	ImGraphCamera&   Camera =  Graph.Camera;

	const float GridSecondarySize = ImGui::GetFontSize() / Camera.Scale;
	const float   GridPrimarySize = GridSecondarySize * Style.GridPrimaryStep;

	const float GridSecondaryStep = GridSecondarySize * Camera.Scale;
	const float   GridPrimaryStep = GridPrimarySize   * Camera.Scale;

	ImVec2 GridStart = ScreenToGrid(grid_bounds.Min);
		   GridStart = ImFloor(GridStart / GridPrimarySize) * GridPrimarySize;
		   GridStart = GridToScreen(GridStart);

	ImVec2 GridEnd = ScreenToGrid(grid_bounds.Max);
		   GridEnd = ImFloor(GridEnd / GridPrimarySize) * GridPrimarySize;
		   GridEnd = GridEnd + ImVec2{ GridPrimarySize, GridPrimarySize };
		   GridEnd = GridToScreen(GridEnd);

	// Secondary Grid
	for(float x = GridStart.x; x < GridEnd.x; x += GridSecondaryStep)
	{
		DrawList.AddLine(
			{ x, 0 }, { x, GridEnd.y }
		,   Style.Colors[ImNodeGraphColor_GridSecondaryLines], Style.GridSecondaryThickness * Camera.Scale
		);
	}

	for(float y = GridStart.y; y < GridEnd.y; y += GridSecondaryStep)
	{
		DrawList.AddLine(
			{ 0, y }, { GridEnd.x, y }
		,   Style.Colors[ImNodeGraphColor_GridSecondaryLines], Style.GridSecondaryThickness * Camera.Scale
		);
	}

	// Primary Grid
	for(float x = GridStart.x; x < GridEnd.x; x += GridPrimaryStep)
	{
		DrawList.AddLine(
			{ x, 0 }, { x, GridEnd.y }
		,   Style.Colors[ImNodeGraphColor_GridPrimaryLines], Style.GridPrimaryThickness * Camera.Scale
		);
	}

	for(float y = GridStart.y; y < GridEnd.y; y += GridPrimaryStep)
	{
		DrawList.AddLine(
			{ 0, y }, { GridEnd.x, y }
		,   Style.Colors[ImNodeGraphColor_GridPrimaryLines], Style.GridPrimaryThickness * Camera.Scale
		);
	}
}

void ImNodeGraph::GraphBehaviour(const ImRect& grid_bounds)
{
	// Context
	ImGuiContext&               Ctx = *ImGui::GetCurrentContext();
    ImGuiIO&                     IO = Ctx.IO;
	ImNodeGraphContext&           G = *GImNodeGraph;
	ImNodeGraphData&          Graph = *G.CurrentGraph;
    ImObjectPool<ImNodeData>& Nodes = Graph.Nodes;
	ImNodeGraphSettings&   Settings = Graph.Settings;
	ImGraphCamera&           Camera = Graph.Camera;


	// Check Focus
	if(!ImGui::IsWindowFocused() || Graph.NewConnection())
	{
	    if(ImGui::IsMouseReleased(ImGuiMouseButton_Left) && Graph.NewConnection())
	    {
	        Graph.NewConnection.Reset();
            ImGui::SetActiveID(0, ImGui::GetCurrentWindow());
	    }
	    return;
	}

	// Vars
	const bool Hovered = ImGui::IsMouseHoveringRect(grid_bounds.Min, grid_bounds.Max);

	// Zooming
	if(Hovered) Graph.TargetZoom += Ctx.IO.MouseWheel * Settings.ZoomRate * Camera.Scale;
	Graph.TargetZoom = ImClamp(Graph.TargetZoom, Settings.ZoomBounds.x, Settings.ZoomBounds.y);
	Camera.Scale     = ImLerp(Camera.Scale, Graph.TargetZoom, Ctx.IO.DeltaTime * Settings.ZoomSmoothing);

    // Select Region
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if(Graph.FocusedNode() == false)
        {
            if(IO.KeyMods == ImGuiMod_None) Graph.Selected.Clear();
        }
        else
        {
            ImVec2 mouse = ScreenToGrid(ImGui::GetMousePos());
            for(ImGuiID node : Graph.Selected) Nodes[node].DragOffset = mouse - Nodes[node].Root;
            Nodes[Graph.FocusedNode].DragOffset = mouse - Nodes[Graph.FocusedNode].Root;
        }
    }

	// Item Focus
	if(ImGui::IsAnyItemFocused()) return;

	// Pin Drag Connection & Node Focus
	if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
        if(Graph.FocusedNode() && !Graph.Dragging)
        {
            Graph.UpdateSelection(Graph.FocusedNode, true);
        }

	    Graph.FocusedNode.Reset();
	    Graph.SelectRegionStart.Reset();
	    Graph.SelectRegion.Clear();
	    Graph.Dragging = false;
	}

    // Dragging Nodes & Region Select
    if(ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        if(Graph.FocusedNode())
        {
            if(!Graph.Selected.Contains(Graph.FocusedNode))
            {
                Graph.UpdateSelection(Graph.FocusedNode, true);
            }

            ImVec2 mouse = ScreenToGrid(ImGui::GetMousePos());
            for(ImGuiID node : Graph.Selected)
            {
                Nodes[node].Root = mouse - Nodes[node].DragOffset;
                if(IO.KeyMods == ImGuiMod_Alt) Nodes[node].Root = SnapToGrid(Nodes[node].Root);
            }
            Graph.Dragging = true;
        }
        else if(Graph.SelectRegionStart() == false && !Graph.LockSelectRegion)
        {
            Graph.SelectRegionStart = ImGui::GetMousePos();
        }
    }

	// Panning
	if(Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
		Graph.IsPanning = true;

	if(ImGui::IsMouseReleased(ImGuiMouseButton_Middle))
		Graph.IsPanning = false;

	if(Graph.IsPanning)
	{
		Camera.Position -= Ctx.IO.MouseDelta / Camera.Scale;
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
	}

    if(ImGui::IsKeyPressed(ImGuiKey_T))
    {
        Nodes.PushToTop(Graph.Nodes.IdxToID[0]);
    }
}

void ImNodeGraph::DrawGraph(ImNodeGraphData* Graph)
{
	ImDrawList&            DrawList = *ImGui::GetWindowDrawList();
	ImDrawListSplitter&    Splitter = DrawList._Splitter;
	ImObjectPool<ImNodeData>& Nodes = Graph->Nodes;
	ImNodeGraphStyle&         Style = Graph->Style;
    ImGraphCamera&           Camera = Graph->Camera;

    ImOptional<ImGuiID> prevFocus = Graph->FocusedNode;
    Graph->HoveredNode.Reset();
    if(ImGui::IsWindowFocused() && !Graph->NewConnection())
    {
        for(auto it = Nodes.rbegin(); it != Nodes.rend(); ++it)
        {
            if(NodeBehaviour(*it)) break;
        }
    }
    if(prevFocus != Graph->FocusedNode)
    {
        Graph->Nodes.PushToTop(Graph->FocusedNode);
    }

	// Draw Nodes
	for(ImNodeData& Node : Nodes)
	{
		SetChannel(Node.BgChannelIndex);
		DrawNode(Node);
	}

	SortChannels();

	Splitter.Merge(&DrawList);

	if(Graph->NewConnection())
	{
		ImPinData& pin = Graph->FindPin(Graph->NewConnection);
		DrawConnection(pin, ImGui::GetMousePos());
	}

    for(int i = 0; i < Graph->Connections.Size(); ++i)
    {
        if(Graph->Connections(i) == false) continue;

        ImPinConnection& connection = Graph->Connections[i];

        if(CheckConnectionValidity(i, connection)) continue;

		DrawConnection(Graph->FindPin(connection.A), Graph->FindPin(connection.B));
    }

    if(Graph->SelectRegionStart())
    {
        ImRect Selection = Graph->GetSelection();

        DrawList.AddRectFilled(
            Selection.Min, Selection.Max
        ,   Style.GetColorU32(ImNodeGraphColor_SelectRegionBackground)
        ,   Style.SelectRegionRounding
        );

        DrawList.AddRect(
            Selection.Min, Selection.Max
        ,   Style.GetColorU32(ImNodeGraphColor_SelectRegionOutline)
        ,   Style.SelectRegionRounding, 0
        ,   Style.SelectRegionOutlineThickness
        );
    }
}

ImVec2 ImNodeGraph::GridToWindow(const ImVec2 &pos)
{
	ImNodeGraphContext&   G = *GImNodeGraph;
	ImNodeGraphData&  Graph = *G.CurrentGraph;

	return GridToScreen(pos) - Graph.Pos;
}

ImVec2 ImNodeGraph::WindowToScreen(const ImVec2 &pos)
{
	ImNodeGraphContext&   G = *GImNodeGraph;
	ImNodeGraphData&  Graph = *G.CurrentGraph;

	return Graph.Pos + pos;
}

ImVec2 ImNodeGraph::GridToScreen(const ImVec2& pos)
{
	ImNodeGraphContext& G = *GImNodeGraph;
	ImNodeGraphData&  Graph = *G.CurrentGraph;
	ImGraphCamera&   Camera = Graph.Camera;

	return (pos - Camera.Position) * Camera.Scale + Graph.GetCenter();
}

ImVec2 ImNodeGraph::ScreenToGrid(const ImVec2& pos)
{
	ImNodeGraphContext& G = *GImNodeGraph;
	IM_ASSERT(G.CurrentGraph);

	ImNodeGraphData&  Graph = *G.CurrentGraph;
	ImGraphCamera&   Camera = Graph.Camera;
	return Camera.Position + (pos - Graph.GetCenter()) / Camera.Scale;
}

ImVec2 ImNodeGraph::ScreenToWindow(const ImVec2 &pos)
{
	ImNodeGraphContext&   G = *GImNodeGraph;
	ImNodeGraphData&  Graph = *G.CurrentGraph;

	return pos - Graph.Pos;
}

ImVec2 ImNodeGraph::WindowToGrid(const ImVec2 &pos)
{
	ImNodeGraphContext&   G = *GImNodeGraph;
	ImNodeGraphData&  Graph = *G.CurrentGraph;

	return ScreenToGrid(Graph.Pos + pos);
}

ImVec2 ImNodeGraph::SnapToGrid(const ImVec2 &pos)
{
    // Draw the grid
    ImGuiWindow& DrawWindow = *ImGui::GetCurrentWindow();
    ImDrawList&    DrawList = *DrawWindow.DrawList;
    ImNodeGraphData&  Graph = *GImNodeGraph->CurrentGraph;
    ImNodeGraphStyle& Style =  Graph.Style;
    ImGraphCamera&   Camera =  Graph.Camera;

    const float GridSecondarySize = ImGui::GetFontSize() / Camera.Scale;

    return ImFloor(pos / GridSecondarySize) * GridSecondarySize;
}

void ImNodeGraph::PushItemWidth(float width)
{
    ImNodeGraphContext&   G = *GImNodeGraph;
    ImNodeGraphData&  Graph = *G.CurrentGraph;

    ImGui::PushItemWidth(Graph.Camera.Scale * width);
}

int ImNodeGraph::PushChannels(int count)
{
	ImDrawList&         DrawList = *ImGui::GetWindowDrawList();
	ImDrawListSplitter& Splitter = DrawList._Splitter;


	// NOTE: this logic has been lifted from ImDrawListSplitter::Split with slight modifications
	// to allow nested splits. The main modification is that we only create new ImDrawChannel
	// instances after splitter._Count, instead of over the whole splitter._Channels array like
	// the regular ImDrawListSplitter::Split method does.

	const int old_channel_capacity = Splitter._Channels.Size;
	// NOTE: _Channels is not resized down, and therefore _Count <= _Channels.size()!
	const int old_channel_count = Splitter._Count;
	const int requested_channel_count = old_channel_count + count;
	if (old_channel_capacity < old_channel_count + count)
	{
		Splitter._Channels.resize(requested_channel_count);
	}

	Splitter._Count = requested_channel_count;

	for (int i = old_channel_count; i < requested_channel_count; ++i)
	{
		ImDrawChannel& channel = Splitter._Channels[i];

		// If we're inside the old capacity region of the array, we need to reuse the existing
		// memory of the command and index buffers.
		if (i < old_channel_capacity)
		{
			channel._CmdBuffer.resize(0);
			channel._IdxBuffer.resize(0);
		}
		// Else, we need to construct new draw channels.
		else
		{
			IM_PLACEMENT_NEW(&channel) ImDrawChannel();
		}

		{
			ImDrawCmd draw_cmd;
			draw_cmd.ClipRect = DrawList._ClipRectStack.back();
			draw_cmd.TextureId = DrawList._TextureIdStack.back();
			channel._CmdBuffer.push_back(draw_cmd);
		}
	}

	return Splitter._Count - count;
}

void ImNodeGraph::SetChannel(ImGuiID id)
{
	ImDrawList*         DrawList = ImGui::GetWindowDrawList();
	ImDrawListSplitter& Splitter = DrawList->_Splitter;
	Splitter.SetCurrentChannel(DrawList, static_cast<int>(id));
}

void ImNodeGraph::SwapChannel(ImDrawChannel& a, ImDrawChannel& b)
{
	a._CmdBuffer.swap(b._CmdBuffer);
	a._IdxBuffer.swap(b._IdxBuffer);
}

void ImNodeGraph::SortChannels()
{
	ImNodeGraphContext&        G = *GImNodeGraph;
	ImNodeGraphData&       Graph = *G.CurrentGraph;
	ImDrawList&         DrawList = *ImGui::GetWindowDrawList();
	ImDrawListSplitter& Splitter = DrawList._Splitter;

    int nump = Graph.Nodes.Active.Size;
	int strt = Splitter._Channels.Size - nump * 2;
	int cnt  = Graph.SubmitCount * 2;

	auto& indices = Graph.Nodes;
	auto& arr = Splitter._Channels;
	ImVector<ImDrawChannel> temp; temp.reserve(cnt);

    for(int i = 0; i < cnt; ++i)
    {
        temp.push_back({ ImVector<ImDrawCmd>(), ImVector<ImDrawIdx>() });
    }

    Splitter.SetCurrentChannel(&DrawList, 0);

	for(int i = 0; i < indices.Size(); ++i)
	{
		if(!indices(i)) continue;

		const int swap_idx = strt + i * 2;
		ImNodeData& node = indices[i];

	    if(node.Graph == nullptr) continue;

		SwapChannel(temp[node.BgChannelIndex - strt], arr[swap_idx]);
		SwapChannel(temp[node.FgChannelIndex - strt], arr[swap_idx + 1]);
	}

	for(int i = 0; i < temp.Size; ++i)
	{
	    SwapChannel(arr[strt + i], temp[i]);
	}
}

bool ImNodeGraph::CheckConnectionValidity(ImGuiID id, ImPinConnection& connection)
{
    ImNodeGraphContext&        G = *GImNodeGraph;
    ImNodeGraphData&       Graph = *G.CurrentGraph;

    ImNodeData* node_a = Graph.Nodes(connection.A.Node) ? &Graph.Nodes[connection.A.Node] : nullptr;
    ImNodeData* node_b = Graph.Nodes(connection.B.Node) ? &Graph.Nodes[connection.B.Node] : nullptr;

    if(node_a == nullptr) { CleanupConnection(id, connection); return true; }
    if(node_b == nullptr) { CleanupConnection(id, connection); return true; }

    if(connection.A.Direction && node_a->OutputPins(connection.A.Pin) == false) { CleanupConnection(id, connection); return true; }
    if(!connection.A.Direction && node_a->InputPins(connection.A.Pin) == false) { CleanupConnection(id, connection); return true; }

    if(connection.B.Direction && node_b->OutputPins(connection.B.Pin) == false) { CleanupConnection(id, connection); return true; }
    if(!connection.B.Direction && node_b->InputPins(connection.B.Pin) == false) { CleanupConnection(id, connection); return true; }

    return false;
}

void ImNodeGraph::CleanupConnection(ImGuiID id, ImPinConnection &connection)
{
    ImNodeGraphContext&        G = *GImNodeGraph;
    ImNodeGraphData&       Graph = *G.CurrentGraph;

    ImNodeData* node_a = Graph.Nodes(connection.A.Node) ? &Graph.Nodes[connection.A.Node] : nullptr;
    ImNodeData* node_b = Graph.Nodes(connection.B.Node) ? &Graph.Nodes[connection.B.Node] : nullptr;

    if(node_a)
    {
        auto& pins = connection.A.Direction ? node_a->OutputPins : node_a->InputPins;
        pins[connection.A.Pin].Connections.find_erase(id);
    }

    if(node_b)
    {
        auto& pins = connection.B.Direction ? node_b->OutputPins : node_b->InputPins;
        pins[connection.B.Pin].Connections.find_erase(id);
    }

    Graph.Connections.Erase(id);
}


// Nodes ---------------------------------------------------------------------------------------------------------------

ImNodeData::ImNodeData()
	: Graph(nullptr)
	, ID(0)
	, Root(0, 0)
	, FgChannelIndex(0), BgChannelIndex(0)
    , Hovered(false), Active(false)
{

}

ImNodeData::ImNodeData(const ImNodeData& other)
	: Graph(other.Graph)
	, ID(other.ID)
	, Root(other.Root)
	, ScreenBounds(other.ScreenBounds)
	, BgChannelIndex(other.BgChannelIndex)
	, FgChannelIndex(other.FgChannelIndex)
    , Hovered(other.Hovered), Active(other.Active)
    , Header(other.Header)
	, InputPins(other.InputPins)
	, OutputPins(other.OutputPins)
{

}

ImNodeData &ImNodeData::operator=(const ImNodeData& other)
{
	if(&other == this) return *this;

	Graph = other.Graph;
	ID = other.ID;
	Root = other.Root;
	ScreenBounds = other.ScreenBounds;
    BgChannelIndex = other.BgChannelIndex;
    FgChannelIndex = other.FgChannelIndex;
    Hovered = other.Hovered;
    Active = other.Active;
    Header = other.Header;
	InputPins = other.InputPins;
	OutputPins = other.OutputPins;

	return *this;
}

void ImNodeGraph::DrawNode(ImNodeData& Node)
{
	ImNodeGraphData*  Graph =  Node.Graph;
	ImNodeGraphStyle& Style =  Graph->Style;
	ImGraphCamera&   Camera =  Graph->Camera;
	ImDrawList&    DrawList = *ImGui::GetCurrentWindow()->DrawList;

	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, Style.NodeOutlineThickness * Camera.Scale);
	ImGui::PushStyleColor(ImGuiCol_Border, Style.GetColorU32(ImNodeGraphColor_NodeOutline));

	ImU32            color = Style.GetColorU32(ImNodeGraphColor_NodeBackground);
	if(Node.Hovered) color = Style.GetColorU32(ImNodeGraphColor_NodeHoveredBackground);
    if(Node.Active)  color = Style.GetColorU32(ImNodeGraphColor_NodeActiveBackground);

	// Render Base Frame
	ImGui::RenderFrame(
		Node.ScreenBounds.Min, Node.ScreenBounds.Max
	,   color, true, Style.NodeRounding * Camera.Scale
	);

	// Render Header
	if(Node.Header())
	{
		// Same as base, but clipped
		ImGui::PushClipRect(Node.Header->ScreenBounds.Min, Node.Header->ScreenBounds.Max, true);
		ImGui::RenderFrame(
			Node.ScreenBounds.Min, Node.ScreenBounds.Max
		,   Node.Header->Color, true, Style.NodeRounding * Camera.Scale
		);
		ImGui::PopClipRect();

		// Border line between header and content
		DrawList.AddLine(
			{ Node.Header->ScreenBounds.Min.x, Node.Header->ScreenBounds.Max.y }
		,   { Node.Header->ScreenBounds.Max.x, Node.Header->ScreenBounds.Max.y }
		,   Style.GetColorU32(ImNodeGraphColor_NodeOutline), Style.NodeOutlineThickness * Camera.Scale
		);
	}

    if(Graph->Selected.Contains(Node))
    {
        DrawList.AddRect(
            Node.ScreenBounds.Min, Node.ScreenBounds.Max
        ,   Style.GetColorU32(ImNodeGraphColor_NodeOutlineSelected)
        ,   Style.NodeRounding * Camera.Scale, 0, Style.NodeOutlineSelectedThickness * Camera.Scale
        );
    }

#ifdef IMNODE_GRAPH_DEBUG_PIN_BOUNDS

    for(ImPinData& pin : Node.InputPins)
    {
        DrawList.AddRect(
            pin.ScreenBounds.Min, pin.ScreenBounds.Max
        ,   ImGui::ColorConvertFloat4ToU32({ 1, 0, 0, 1 })
        ,   0, 0, 2
        );
    }

    for(ImPinData& pin : Node.OutputPins)
    {
        DrawList.AddRect(
            pin.ScreenBounds.Min, pin.ScreenBounds.Max
        ,   ImGui::ColorConvertFloat4ToU32({ 1, 0, 0, 1 })
        ,   0, 0, 2
        );
    }

#endif

	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

bool ImNodeGraph::NodeBehaviour(ImNodeData& Node)
{
	ImNodeGraphData& Graph = *Node.Graph;

    bool is_focus   = Graph.FocusedNode == Node;

    if(Node.Hovered) Graph.HoveredNode = Node;
    if(Node.Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        Graph.FocusedNode = Node;
    }

    // Select Region
    if(Graph.SelectRegionStart())
    {
        bool intersect = ImAABB(Graph.GetSelection(), Node.ScreenBounds);
        bool checked   = Graph.SelectRegion.Contains(Node);

        if(intersect && !checked)
        {
            Graph.SelectRegion.Insert(Node);
            Graph.UpdateSelection(Node);
        }

        if(!intersect && checked)
        {
            Graph.SelectRegion.Erase(Node);
            Graph.UpdateSelection(Node, false, true);
        }
    }

    Node.Active = is_focus;

    if(Node.Active) ImGui::SetActiveID(Node.ID, ImGui::GetCurrentWindow());

    return false;
}

// Pins ----------------------------------------------------------------------------------------------------------------


ImPinData::ImPinData()
    : Node(0)
    , ID(0)
    , Type(0)
    , Direction(ImPinDirection_Input)
    , Flags(ImPinFlags_None)
    , Hovered(false)
{ }

void ImNodeGraph::PinHead(ImGuiID id, ImPinData& Pin)
{
	ImNodeGraphData&        Graph = *GImNodeGraph->CurrentGraph;
	const ImGraphCamera&   Camera =  Graph.Camera;
	const ImNodeGraphStyle& Style =  Graph.Style;
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, Style.PinOutlineThickness * Camera.Scale);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    ImVec2{ Style.ItemSpacing, Style.ItemSpacing } * Camera.Scale);

	ImGuiWindow&  Window = *ImGui::GetCurrentWindow();
	ImDrawList& DrawList = *Window.DrawList;
	ImGuiStyle& ImStyle = ImGui::GetStyle();
	const ImVec2 label_size = ImGui::CalcTextSize("##", NULL, true);

	// Modified Radio Button to get proper framing
	const float square_sz = ImGui::GetFrameHeight();
	const ImVec2 pos = Window.DC.CursorPos;
	const ImRect check_bb(pos, pos + ImVec2(square_sz, square_sz));
	const ImRect total_bb(pos, pos + ImVec2(square_sz + (label_size.x > 0.0f ? ImStyle.ItemInnerSpacing.x + label_size.x : 0.0f), label_size.y + ImStyle.FramePadding.y * 2.0f));
	Pin.Center = check_bb.GetCenter();
	const float  radius = Style.PinRadius * Camera.Scale;
	const float outline = Style.PinOutlineThickness * Camera.Scale;

    // Behaviour
    bool pressed = false, filled = !Pin.Connections.empty();
    if(ImGui::IsWindowFocused())
    {
        Pin.Hovered = ImGui::IsMouseHoveringRect(check_bb.Min, check_bb.Max);
        pressed = (Pin.Hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left));
        filled |= (Pin.Hovered || Graph.NewConnection == Pin);

        // Start new connection when left clicked
        if(Pin.Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyModKeyDown())
        {
            BeginConnection(Pin);
            ImGui::SetActiveID(id, ImGui::GetCurrentWindow());
        }

        // Dropping new connection
        if(Pin.Hovered && Graph.NewConnection() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            ImPinData& other = Graph.FindPin(Graph.NewConnection);

            MakeConnection(Pin, other);
        }

        // Break connections with Alt-Left-Click
        if(Pin.Hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::IsKeyDown(ImGuiMod_Alt) && !Graph.NewConnection())
            BreakConnections(Pin);
    }


	// Item for ImGui
	ImGui::ItemSize(total_bb, ImStyle.FramePadding.y);
	ImGui::ItemAdd(total_bb, id, &check_bb);
    ImGui::ItemHoverable(check_bb, id, ImGuiHoveredFlags_None);

	// Drawing
	ImVec4 PinColor  = Style.PinColors[Pin.Type].Value;
	PinColor = PinColor * (pressed ? 0.8f : 1.0f);
	ImVec4 FillColor = filled ? PinColor : Style.GetColorVec4(ImNodeGraphColor_PinBackground);

	if(pressed || filled)
	{
		DrawList.AddCircleFilled(Pin.Center, radius + outline * 0.5f, ImGui::ColorConvertFloat4ToU32(FillColor));
	}
	else
	{
		DrawList.AddCircleFilled(Pin.Center, radius, ImGui::ColorConvertFloat4ToU32(FillColor));
		DrawList.AddCircle(Pin.Center, radius, ImGui::ColorConvertFloat4ToU32(PinColor), 0, outline);
	}

	ImGui::SameLine();
	ImGui::PopStyleVar(2);
}

void ImNodeGraph::DummyPinHead(ImPinData& Pin)
{
	ImNodeGraphData*        Graph = GImNodeGraph->CurrentGraph;
	const ImGraphCamera&   Camera = Graph->Camera;
	const ImNodeGraphStyle& Style = Graph->Style;
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, Style.PinOutlineThickness * Camera.Scale);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    ImVec2{ Style.ItemSpacing, Style.ItemSpacing } * Camera.Scale);

	ImGuiWindow& Window = *ImGui::GetCurrentWindow();
	ImGuiStyle& ImStyle = ImGui::GetStyle();
	const ImVec2 label_size = ImGui::CalcTextSize("##", NULL, true);

	const float square_sz = ImGui::GetFrameHeight();
	const ImVec2 pos = Window.DC.CursorPos;
	const ImRect total_bb(pos, pos + ImVec2(square_sz + (label_size.x > 0.0f ? ImStyle.ItemInnerSpacing.x + label_size.x : 0.0f), label_size.y + ImStyle.FramePadding.y * 2.0f));

	ImGui::ItemSize(total_bb, ImStyle.FramePadding.y);
	ImGui::ItemAdd(total_bb, -1);

	ImGui::SameLine();
	ImGui::PopStyleVar(2);
}


// Connections ---------------------------------------------------------------------------------------------------------

void ImNodeGraph::BeginConnection(const ImPinPtr &pin)
{
	ImNodeGraphContext&  G = *GImNodeGraph;
	ImNodeGraphData& Graph = *G.CurrentGraph;
	Graph.NewConnection = pin;
}

bool ImNodeGraph::MakeConnection(const ImPinPtr &a, const ImPinPtr &b)
{
	if(a.Direction == b.Direction) return false;
	if(a.Node == b.Node)           return false;

	ImNodeGraphContext&  G = *GImNodeGraph;
	ImNodeGraphData& Graph = *G.CurrentGraph;

	ImPinData& A = Graph.FindPin(a);
	ImPinData& B = Graph.FindPin(b);

    if(Graph.Validation && Graph.Validation(A, B)) return false;

    if(A.Direction == ImPinDirection_Input && !A.Connections.empty()) BreakConnections(A);
    if(B.Direction == ImPinDirection_Input && !B.Connections.empty()) BreakConnections(B);

	ImGuiID connId = Graph.Connections.Insert({ a, b });

	A.Connections.push_back(connId);
	B.Connections.push_back(connId);

    A.NewConnections.push_back(b); A.BNewConnections = true;
    B.NewConnections.push_back(a); B.BNewConnections = true;

    return true;
}

void ImNodeGraph::BreakConnection(ImGuiID id)
{
	ImNodeGraphContext&  G = *GImNodeGraph;
	ImNodeGraphData& Graph = *G.CurrentGraph;

	ImPinConnection connection = Graph.Connections[id]; Graph.Connections.Erase(id);
	ImPinData& A = Graph.FindPin(connection.A);
	ImPinData& B = Graph.FindPin(connection.B);

	A.Connections.find_erase_unsorted(id);
	B.Connections.find_erase_unsorted(id);

    A.ErasedConnections.push_back(connection.B); A.BErasedConnections = true;
    B.ErasedConnections.push_back(connection.A); B.BErasedConnections = true;

    ImPinPtr* it;
    if((it = A.NewConnections.find(B)) != A.NewConnections.end()) A.NewConnections.erase(it);
    if((it = B.NewConnections.find(A)) != B.NewConnections.end()) B.NewConnections.erase(it);
}

void ImNodeGraph::BreakConnections(const ImPinPtr &ptr)
{
	ImNodeGraphContext&  G = *GImNodeGraph;
	ImNodeGraphData& Graph = *G.CurrentGraph;
	ImPinData&         Pin =  Graph.FindPin(ptr);

	for(ImGuiID id : Pin.Connections)
	{
		ImPinConnection connection = Graph.Connections[id]; Graph.Connections.Erase(id);
		ImPinData& other = Graph.FindPin((connection.A == ptr) ? connection.B : connection.A);

	    Pin.ErasedConnections.push_back(other); Pin.BErasedConnections   = true;
	    other.ErasedConnections.push_back(ptr); other.BErasedConnections = true;

	    ImPinPtr* it;
	    if((it = Pin.NewConnections.find(other)) != Pin.NewConnections.end())   Pin.NewConnections.erase(it);
        if((it = other.NewConnections.find(Pin)) != other.NewConnections.end()) other.NewConnections.erase(it);

		other.Connections.find_erase_unsorted(id);
	}

	Pin.Connections.clear();
}

void ImNodeGraph::DrawConnection(const ImVec2& out, const ImVec4& out_col, const ImVec2& in, const ImVec4& in_col)
{
	ImGuiWindow&  Window = *ImGui::GetCurrentWindow();
	ImDrawList& DrawList = *Window.DrawList;
	ImNodeGraphContext&   G = *GImNodeGraph;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	const ImGraphCamera&   Camera =  Graph->Camera;
	const ImNodeGraphStyle& Style =  Graph->Style;

	// Calculate Bezier Derivatives
	const float FrameHeight = ImGui::GetFrameHeight();
	const float diff_x = out.x - in.x;
	const float diff_y = out.y - in.y;
	const float y_weight = ImAbs(diff_y);
	const float xy_ratio = 1.0f + ImMax(diff_x, 0.0f) / (FrameHeight + ImAbs(diff_y));
	const float offset = y_weight * xy_ratio;

	const ImVec2 out_v = ImVec2(out.x + offset, out.y);
	const ImVec2 in_v  = ImVec2(in.x - offset,  in.y);

	AddBezierCubicMultiColored(DrawList, in, in_v, out_v, out, in_col, out_col, Style.ConnectionThickness * Camera.Scale);
}

void ImNodeGraph::DrawConnection(const ImPinData& pin, const ImVec2& point)
{
	ImNodeGraphData*        Graph = GImNodeGraph->CurrentGraph;
	const ImNodeGraphStyle& Style = Graph->Style;

	if(pin.Direction) DrawConnection(PinConnectionAnchor(pin), Style.PinColors[pin.Type], point, Style.PinColors[pin.Type]);
	else              DrawConnection(point, Style.PinColors[pin.Type], PinConnectionAnchor(pin), Style.PinColors[pin.Type]);
}

void ImNodeGraph::DrawConnection(const ImPinData &a, const ImPinData &b)
{
	ImNodeGraphData*        Graph = GImNodeGraph->CurrentGraph;
	const ImNodeGraphStyle& Style = Graph->Style;

	const ImVec2& a_anchor = PinConnectionAnchor(a);
	const ImVec4& a_col    = Style.PinColors[a.Type];
	const ImVec2& b_anchor = PinConnectionAnchor(b);
	const ImVec4& b_col    = Style.PinColors[b.Type];

	const ImVec2& out = a.Direction ? a_anchor : b_anchor;
	const ImVec2& in  = a.Direction ? b_anchor : a_anchor;

	const ImVec4& out_col = a.Direction ? a_col : b_col;
	const ImVec4& in_col  = a.Direction ? b_col : a_col;

	DrawConnection(out, out_col, in, in_col);
}

ImVec2 ImNodeGraph::PinConnectionAnchor(const ImPinData &Pin)
{
	ImNodeGraphData*        Graph =  GImNodeGraph->CurrentGraph;
	const ImGraphCamera&   Camera =  Graph->Camera;
	const ImNodeGraphStyle& Style =  Graph->Style;
	const float  radius = Style.PinRadius * Camera.Scale;
	ImVec2 loc = Pin.Center;
	loc += ImVec2(radius, 0) * (Pin.Direction ? 1.0f : -1.0f);
	return loc;
}

// On AddPolyline() and AddConvexPolyFilled() we intentionally avoid using ImVec2 and superfluous function calls to optimize debug/non-inlined builds.
// - Those macros expects l-values and need to be used as their own statement.
// - Those macros are intentionally not surrounded by the 'do {} while (0)' idiom because even that translates to runtime with debug compilers.
#define IM_NORMALIZE2F_OVER_ZERO(VX,VY)     { float d2 = VX*VX + VY*VY; if (d2 > 0.0f) { float inv_len = ImRsqrt(d2); VX *= inv_len; VY *= inv_len; } } (void)0
#define IM_FIXNORMAL2F_MAX_INVLEN2          100.0f // 500.0f (see #4053, #3366)
#define IM_FIXNORMAL2F(VX,VY)               { float d2 = VX*VX + VY*VY; if (d2 > 0.000001f) { float inv_len2 = 1.0f / d2; if (inv_len2 > IM_FIXNORMAL2F_MAX_INVLEN2) inv_len2 = IM_FIXNORMAL2F_MAX_INVLEN2; VX *= inv_len2; VY *= inv_len2; } } (void)0

void ImNodeGraph::AddPolylineMultiColored(ImDrawList &draw_list, const ImVec2 *points, int num_points, const ImVec4 &c1,
                                          const ImVec4 &c2, ImDrawFlags flags, float thickness)
{
    if (num_points < 2) return;

    const int count = num_points - 1; // The number of line segments we need to draw
    const bool thick_line = (thickness > draw_list._FringeScale);
    const ImVec2 opaque_uv = draw_list._Data->TexUvWhitePixel;

    draw_list._Data->TempBuffer.reserve_discard(num_points * 2);
    ImVec2* normals = draw_list._Data->TempBuffer.Data;
    ImU32* colors   = reinterpret_cast<ImU32*>(normals + num_points);
    for(int i = 0; i < count; ++i)
    {
        const ImVec2 &a = points[i], &b = points[(i + 1) % num_points];
        normals[i] = b - a;
        IM_NORMALIZE2F_OVER_ZERO(normals[i].x, normals[i].y);
        normals[i] = { normals[i].y, -normals[i].x };
        colors[i]  = ImGui::ColorConvertFloat4ToU32({ ImLerp(c1, c2, i / static_cast<float>(num_points)) });
    }
    colors[num_points - 1]  = ImGui::ColorConvertFloat4ToU32(c2);
    normals[num_points - 1] = normals[num_points - 2];

    for(int i = 1; i < count; ++i)
    {
        normals[i] = (normals[i] + normals[i + 1]) * 0.5f;
        IM_FIXNORMAL2F(normals[i].x, normals[i].y);
    }

    const float AA_SIZE = draw_list._FringeScale;

    // Thicknesses <1.0 should behave like thickness 1.0
    thickness = ImMax(thickness, 1.0f);
    const int integer_thickness = (int)thickness;
    const float half_inner_thickness = (thickness - AA_SIZE) * 0.5f;

    const int idx_count = thick_line ? count * 18     : count * 12;
    const int vtx_count = thick_line ? num_points * 4 : num_points * 3;
    draw_list.PrimReserve(idx_count, vtx_count);

    ImDrawIdx*  _IdxWritePtr = draw_list._IdxWritePtr;
    ImDrawVert* _VtxWritePtr = draw_list._VtxWritePtr;

    for(int i = 0; i <= count; ++i)
    {
        if(thick_line)
        {
            const int v1 = draw_list._VtxCurrentIdx + i * 4;
            const int v2 = draw_list._VtxCurrentIdx + i * 4 + 4;

            const int i1 = i;

            const ImVec2 n1 = normals[i1] * (half_inner_thickness + AA_SIZE);
            const ImVec2 n2 = normals[i1] * (half_inner_thickness);

            // first points
            _VtxWritePtr[0].pos = points[i1] + n1; _VtxWritePtr[0].uv = opaque_uv; _VtxWritePtr[0].col = colors[i1] & ~IM_COL32_A_MASK;
            _VtxWritePtr[1].pos = points[i1] + n2; _VtxWritePtr[1].uv = opaque_uv; _VtxWritePtr[1].col = colors[i1];
            _VtxWritePtr[2].pos = points[i1] - n2; _VtxWritePtr[2].uv = opaque_uv; _VtxWritePtr[2].col = colors[i1];
            _VtxWritePtr[3].pos = points[i1] - n1; _VtxWritePtr[3].uv = opaque_uv; _VtxWritePtr[3].col = colors[i1] & ~IM_COL32_A_MASK;

            _VtxWritePtr += 4;

            if(i == count) continue;

            // top
            _IdxWritePtr[0] = v1 + 0; _IdxWritePtr[1] = v2 + 0; _IdxWritePtr[2] = v1 + 1;
            _IdxWritePtr[3] = v1 + 1; _IdxWritePtr[4] = v2 + 0; _IdxWritePtr[5] = v2 + 1;

            // middle
            _IdxWritePtr[6] = v1 + 1; _IdxWritePtr[7]  = v2 + 1; _IdxWritePtr[8]  = v1 + 2;
            _IdxWritePtr[9] = v1 + 2; _IdxWritePtr[10] = v2 + 1; _IdxWritePtr[11] = v2 + 2;

            // bottom
            _IdxWritePtr[12] = v1 + 2; _IdxWritePtr[13] = v2 + 2; _IdxWritePtr[14] = v1 + 3;
            _IdxWritePtr[15] = v1 + 3; _IdxWritePtr[16] = v2 + 2; _IdxWritePtr[17] = v2 + 3;

            _IdxWritePtr += 18;
        }
        else
        {
            const int v1 = draw_list._VtxCurrentIdx + i * 4;
            const int v2 = draw_list._VtxCurrentIdx + i * 4 + 4;

            const int i1 = i;

            const ImVec2 n = normals[i1] * AA_SIZE;

            // first points
            _VtxWritePtr[0].pos = points[i1] + n; _VtxWritePtr[0].uv = opaque_uv; _VtxWritePtr[0].col = colors[i1] & ~IM_COL32_A_MASK;
            _VtxWritePtr[1].pos = points[i1];     _VtxWritePtr[1].uv = opaque_uv; _VtxWritePtr[1].col = colors[i1];
            _VtxWritePtr[2].pos = points[i1] - n; _VtxWritePtr[2].uv = opaque_uv; _VtxWritePtr[2].col = colors[i1] & ~IM_COL32_A_MASK;

            _VtxWritePtr += 3;

            if(i == count) continue;

            // top
            _IdxWritePtr[0] = v1 + 0; _IdxWritePtr[1] = v2 + 0; _IdxWritePtr[2] = v1 + 1;
            _IdxWritePtr[3] = v1 + 1; _IdxWritePtr[4] = v2 + 0; _IdxWritePtr[5] = v2 + 1;

            // bottom
            _IdxWritePtr[6] = v1 + 1; _IdxWritePtr[7]  = v2 + 1; _IdxWritePtr[8]  = v1 + 2;
            _IdxWritePtr[9] = v1 + 2; _IdxWritePtr[10] = v2 + 1; _IdxWritePtr[11] = v2 + 2;

            _IdxWritePtr += 12;
        }
    }

    draw_list._VtxCurrentIdx += static_cast<unsigned int>(_VtxWritePtr - draw_list._VtxWritePtr);
    draw_list._VtxWritePtr = _VtxWritePtr;
    draw_list._IdxWritePtr = _IdxWritePtr;
}

#define IM_FIXNORMAL2F_MAX_INVLEN2          100.0f // 500.0f (see #4053, #3366)

#define IM_FIXNORMAL2F(VX,VY)               { float d2 = VX*VX + VY*VY; if (d2 > 0.000001f) { float inv_len2 = 1.0f / d2; if (inv_len2 > IM_FIXNORMAL2F_MAX_INVLEN2) inv_len2 = IM_FIXNORMAL2F_MAX_INVLEN2; VX *= inv_len2; VY *= inv_len2; } } (void)0

void ImNodeGraph::AddBezierCubicMultiColored(ImDrawList& draw_list, const ImVec2 &p1, const ImVec2 &p2, const ImVec2 &p3, const ImVec2 &p4,
                                             const ImVec4 &c1, const ImVec4 &c2, float thickness, int num_segments)
{
	draw_list.PathLineTo(p1);
	draw_list.PathBezierCubicCurveTo(p2, p3, p4, num_segments);
	PathStrokeMultiColored(draw_list, c1, c2, 0, thickness);
}


// =====================================================================================================================
// Public Functionality
// =====================================================================================================================

// Context -------------------------------------------------------------------------------------------------------------

ImNodeGraphContext* ImNodeGraph::CreateContext()
{
	// Get current context
	ImNodeGraphContext* prev_ctx = GetCurrentContext();

	// Create new context
	ImNodeGraphContext* ctx = IM_NEW(ImNodeGraphContext)();
	SetCurrentContext(ctx);
	Initialize();

	// If there is a previous context, restore it
	if(prev_ctx != nullptr) SetCurrentContext(prev_ctx);

	// Return the new context
	return ctx;
}

void ImNodeGraph::DestroyContext(ImNodeGraphContext* ctx)
{
	// Get current context
	ImNodeGraphContext* prev_ctx = GetCurrentContext();

	// If the provided context is null, use the current context
	if(ctx == nullptr) ctx = prev_ctx;

	// Shutdown the context to destroy
	SetCurrentContext(ctx);
	Shutdown();

	// Restore or clear the context
	SetCurrentContext((prev_ctx == ctx) ? nullptr : prev_ctx);

	// Free context memory
	IM_DELETE(ctx);
}

ImNodeGraphContext * ImNodeGraph::GetCurrentContext()
{
	return GImNodeGraph;
}

void ImNodeGraph::SetCurrentContext(ImNodeGraphContext *ctx)
{
	GImNodeGraph = ctx;
}

void ImNodeGraph::AddFont(const char * path, float size, const ImWchar* glyph_ranges)
{
	ImNodeFontConfig cfg{ ImStrdup(path), size, glyph_ranges };
	GFonts.push_back(cfg);
}


// Graph ---------------------------------------------------------------------------------------------------------------

ImGraphCamera::ImGraphCamera() : Position{ 0, 0 }, Scale(1.0f) {}

ImNodeGraphStyle::ImNodeGraphStyle()
	: GridPrimaryStep(5)
	, GridPrimaryThickness(2.0f)
	, GridSecondaryThickness(1.0f)

	, NodePadding(8.0f)
	, NodeRounding(8.0f)
	, NodeOutlineThickness(2.0f)
	, NodeOutlineSelectedThickness(4.0f)

	, SelectRegionRounding(2.0f)
	, SelectRegionOutlineThickness(2.0f)

	, ItemSpacing(4.0f)
	, PinRadius(8.0f)
	, PinOutlineThickness(3.0f)

	, ConnectionThickness(2.0f)

	, Colors{ ImColor(0x000000FF) }
	, PinColors(nullptr)
{
	Colors[ImNodeGraphColor_GridBackground]     = ImColor(0x11, 0x11, 0x11);
	Colors[ImNodeGraphColor_GridPrimaryLines]   = ImColor(0x88, 0x88, 0x88);
	Colors[ImNodeGraphColor_GridSecondaryLines] = ImColor(0x44, 0x44, 0x44);

	Colors[ImNodeGraphColor_NodeBackground]        = ImColor(0x88, 0x88, 0x88);
	Colors[ImNodeGraphColor_NodeHoveredBackground] = ImColor(0x9C, 0x9C, 0x9C);
	Colors[ImNodeGraphColor_NodeActiveBackground]  = ImColor(0x7A, 0x7A, 0x7A);
	Colors[ImNodeGraphColor_NodeOutline]           = ImColor(0x33, 0x33, 0x33);
	Colors[ImNodeGraphColor_NodeOutlineSelected]   = ImColor(0xEF, 0xAE, 0x4B);

	Colors[ImNodeGraphColor_PinBackground] = ImColor(0x22, 0x22, 0x22);

	Colors[ImNodeGraphColor_SelectRegionBackground] = ImColor(0xC9, 0x8E, 0x36, 0x44);
	Colors[ImNodeGraphColor_SelectRegionOutline]    = ImColor(0xEF, 0xAE, 0x4B, 0xBB);
}

ImNodeGraphSettings::ImNodeGraphSettings()
	: ZoomRate(0.1f)
	, ZoomSmoothing(8.0f)
	, ZoomBounds(0.6f, 2.5f)
{

}

void ImNodeGraph::BeginGraph(const char* title, const ImVec2& size_arg)
{
	// Validate Global State
	IM_ASSERT(GImNodeGraph != nullptr);
	ImNodeGraphContext& G = *GImNodeGraph;

	// Ensure we are in the scope of a window
	ImGuiWindow* Window = ImGui::GetCurrentWindow();
	IM_ASSERT(Window != nullptr);                       // Ensure we are within a window

	// Validate parameters and graph state
	IM_ASSERT(title != nullptr && title[0] != '\0');    // Graph name required
	IM_ASSERT(G.Scope == ImNodeGraphScope_None);     // Ensure we are not in the scope of another graph

	// Get Graph
	ImNodeGraphData* Graph = FindGraphByTitle(title);
	const bool FirstFrame = (Graph == nullptr);
	if(FirstFrame) { Graph = CreateNewGraph(title); }

	ImGraphCamera& Camera = Graph->Camera;

	// Update State
	G.CurrentGraph = Graph;
	G.Scope        = ImNodeGraphScope_Graph;

	// Style & Settings
	ImNodeGraphStyle& Style = Graph->Style;

	// Fonts
	G.Fonts.front()->Scale = Camera.Scale / GFontUpscale;
	ImGui::PushFont(G.Fonts.front());

	// Calculate Size
	const ImVec2 SizeAvail = ImGui::GetContentRegionAvail();
	const ImVec2 Size = ImGui::CalcItemSize(size_arg, SizeAvail.x, SizeAvail.y);
	Graph->Size = Size;
	Graph->Pos  = ImGui::GetCursorScreenPos();
	Graph->SubmitCount = 0;
    Graph->LockSelectRegion = false;

    // Cleanup erased nodes
	int cnt = Graph->Nodes.Cleanup();
    for(int i = ImMax(Graph->Nodes.Freed.Size - cnt - 1, 0); i < Graph->Nodes.Freed.Size; ++i)
    {
        Graph->Selected.Erase(Graph->Nodes.IdxToID[Graph->Nodes.Freed[i]]);
    }

    // Reset nodes
    Graph->Nodes.Reset();

	// Begin the Graph Child
	ImGui::PushStyleColor(ImGuiCol_ChildBg, static_cast<ImU32>(Style.Colors[ImNodeGraphColor_GridBackground]));
	ImGui::BeginChild(Graph->ID, Size, 0, ImGuiWindowFlags_NoScrollbar);
	ImGui::PopStyleColor();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2{ Style.ItemSpacing, Style.ItemSpacing } * Camera.Scale);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2{ Style.ItemSpacing, Style.ItemSpacing } * Camera.Scale);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,     ImVec2{ Style.NodePadding, Style.NodePadding } * Camera.Scale);
	DrawGrid({ Graph->Pos, Graph->Pos + Graph->Size });
}

void ImNodeGraph::EndGraph()
{
	// Validate global state
	IM_ASSERT(GImNodeGraph != nullptr);
	ImNodeGraphContext&  G = *GImNodeGraph;

	// Validate graph state
	ImNodeGraphData* Graph = G.CurrentGraph;
	IM_ASSERT(G.Scope == ImNodeGraphScope_Graph && Graph != nullptr); // Ensure we are in the scope of a graph

	DrawGraph(Graph);

	if(ImGui::IsWindowFocused()) GraphBehaviour({ Graph->Pos, Graph->Pos + Graph->Size });

	ImGui::PopStyleVar(3);
	ImGui::PopFont();
	ImGui::EndChild();

	// Update State
	G.CurrentGraph = nullptr;
	G.Scope        = ImNodeGraphScope_None;
}

void ImNodeGraph::BeginGraphPostOp(const char *title)
{
    // Validate Global State
    IM_ASSERT(GImNodeGraph != nullptr);
    ImNodeGraphContext& G = *GImNodeGraph;

    // Ensure we are in the scope of a window
    ImGuiWindow* Window = ImGui::GetCurrentWindow();
    IM_ASSERT(Window != nullptr);                       // Ensure we are within a window

    // Validate parameters and graph state
    IM_ASSERT(title != nullptr && title[0] != '\0');    // Graph name required
    IM_ASSERT(G.Scope == ImNodeGraphScope_None);     // Ensure we are not in the scope of another graph

    // Get Graph
    ImNodeGraphData* Graph = FindGraphByTitle(title);

    // Update State
    G.CurrentGraph = Graph;
    G.Scope        = ImNodeGraphScope_Graph;
}

void ImNodeGraph::EndGraphPostOp()
{
    // Validate global state
    IM_ASSERT(GImNodeGraph != nullptr);
    ImNodeGraphContext&  G = *GImNodeGraph;

    // Validate graph state
    ImNodeGraphData* Graph = G.CurrentGraph;
    IM_ASSERT(G.Scope == ImNodeGraphScope_Graph && Graph != nullptr); // Ensure we are in the scope of a graph

    // Update State
    G.CurrentGraph = nullptr;
    G.Scope        = ImNodeGraphScope_None;
}

void ImNodeGraph::SetGraphValidation(ImConnectionValidation validation)
{
    // Validate global state
    IM_ASSERT(GImNodeGraph != nullptr);
    ImNodeGraphContext&  G = *GImNodeGraph;

    // Validate graph state
    ImNodeGraphData* Graph = G.CurrentGraph;
    IM_ASSERT(G.Scope != ImNodeGraphScope_None && Graph != nullptr); // Ensure we are in the scope of a graph

    Graph->Validation = validation;
}

float ImNodeGraph::GetCameraScale()
{
	// Validate global state
	IM_ASSERT(GImNodeGraph != nullptr);
	ImNodeGraphContext&  G = *GImNodeGraph;

	// Validate graph state
	ImNodeGraphData* Graph = G.CurrentGraph;
	IM_ASSERT(G.Scope != ImNodeGraphScope_None && Graph != nullptr); // Ensure we are in the scope of a graph

	return Graph->Camera.Scale;
}


// Node ----------------------------------------------------------------------------------------------------------------

void ImNodeGraph::BeginNode(const char* title, ImVec2& pos)
{
    IM_ASSERT(GImNodeGraph != nullptr);

    // Validate State
    ImNodeGraphContext&   G = *GImNodeGraph;
    ImNodeGraphData*  Graph =  G.CurrentGraph;
    IM_ASSERT(G.Scope == ImNodeGraphScope_Graph && Graph != nullptr); // Ensure we are in the scope of a graph

    // Get Node
    ImGuiID id = ImGui::GetCurrentWindow()->GetID(title);
    ImNodeData& Node = Graph->Nodes[id];
    if(Node.Graph == nullptr)
    {
        Node.Graph = Graph;
        Node.Root = pos;
        Node.ID = id;
        Node.UserID.String = title;
    }

    // Style
    const ImNodeGraphStyle& Style = Graph->Style;

    // Update node vars
    Node.InputPins.Cleanup();  Node.InputPins.Reset();
    Node.OutputPins.Cleanup(); Node.OutputPins.Reset();
    Node.Header.Reset();
    pos = Node.Root;

    // Push Scope
    Graph->CurrentNode = &Node;
    Graph->SubmitCount ++;
    G.Scope = ImNodeGraphScope_Node;

    // Push new draw channels
    Node.BgChannelIndex = PushChannels(2);
    Node.FgChannelIndex = Node.BgChannelIndex + 1;
    SetChannel(Node.FgChannelIndex);

    // Setup Node Group
    ImGui::SetCursorScreenPos(GridToScreen(pos + ImVec2(Style.NodePadding, Style.NodePadding)));
    ImGui::BeginGroup();
    ImGui::PushID(static_cast<int>(id));

    ImGuiContext& Ctx = *ImGui::GetCurrentContext();
    Node.PrevActiveItem = Ctx.ActiveId;
}

void ImNodeGraph::BeginNode(int iid, ImVec2& pos)
{
	IM_ASSERT(GImNodeGraph != nullptr);

	// Validate State
	ImNodeGraphContext&   G = *GImNodeGraph;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	IM_ASSERT(G.Scope == ImNodeGraphScope_Graph && Graph != nullptr); // Ensure we are in the scope of a graph

	// Get Node
    ImGuiID id = ImGui::GetCurrentWindow()->GetID(iid);
    ImNodeData& Node = Graph->Nodes[id];
    if(Node.Graph == nullptr)
    {
        Node.Graph = Graph;
        Node.Root = pos;
        Node.ID = id;
        Node.UserID.Int = iid;
    }

	// Style
	const ImNodeGraphStyle& Style = Graph->Style;

	// Update node vars
	Node.InputPins.Cleanup();  Node.InputPins.Reset();
	Node.OutputPins.Cleanup(); Node.OutputPins.Reset();
	Node.Header.Reset();
	pos = Node.Root;

	// Push Scope
	Graph->CurrentNode = &Node;
	Graph->SubmitCount ++;
	G.Scope = ImNodeGraphScope_Node;

	// Push new draw channels
	Node.BgChannelIndex = PushChannels(2);
	Node.FgChannelIndex = Node.BgChannelIndex + 1;
	SetChannel(Node.FgChannelIndex);

	// Setup Node Group
	ImGui::SetCursorScreenPos(GridToScreen(pos + ImVec2(Style.NodePadding, Style.NodePadding)));
    ImGui::BeginGroup();
    ImGui::PushID(static_cast<int>(id));

    ImGuiContext& Ctx = *ImGui::GetCurrentContext();
    Node.PrevActiveItem = Ctx.ActiveId;
}

void ImNodeGraph::EndNode()
{
	// Validate global state
	IM_ASSERT(GImNodeGraph != nullptr);

	// Validate graph state
	ImNodeGraphContext&   G = *GImNodeGraph;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	IM_ASSERT(Graph != nullptr);
	IM_ASSERT(G.Scope == ImNodeGraphScope_Node && Graph->CurrentNode != nullptr); // Ensure we are in the scope of a node

    ImNodeData& Node = *Graph->CurrentNode;
    ImGuiContext& Ctx = *ImGui::GetCurrentContext();
    if(Ctx.ActiveId != Node.PrevActiveItem || Ctx.ActiveId == 0) Node.ActiveItem = Ctx.ActiveId;

    bool is_node_item_active = Ctx.ActiveId == Node.ActiveItem && Ctx.ActiveId != 0;
    bool other_hovered = ImGui::IsAnyItemHovered() || is_node_item_active;
    if(other_hovered) Graph->LockSelectRegion = true;

    ImGui::PopID();
    ImGui::EndGroup();

    const ImNodeGraphStyle& Style = Graph->Style;
	const ImGraphCamera&   Camera = Graph->Camera;

	Node.ScreenBounds = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
    Node.ScreenBounds.Expand(Style.NodePadding * Camera.Scale);

    bool hovering      = ImGui::IsMouseHoveringRect(Node.ScreenBounds.Min, Node.ScreenBounds.Max) && !other_hovered;
    bool is_focus      = Graph->FocusedNode == Node;
    bool is_hovered    = Graph->HoveredNode == Node;

    // Fixup pins
    float Width  = 0;
    auto  Input  = Node.InputPins.begin();
    auto  Output = Node.OutputPins.begin();
    const auto InputEnd = Node.InputPins.end();
    const auto OutputEnd = Node.OutputPins.end();

    while(Input != InputEnd || Output != OutputEnd)
    {
        float iWidth =  Input != InputEnd  ? Input->ScreenBounds.GetWidth() : 0;
        float oWidth = Output != OutputEnd ? Output->ScreenBounds.GetWidth() : 0;
        Width = ImMax(Width, iWidth + oWidth);

        if(Input  != InputEnd)  { if(Input->Hovered)  hovering = false; ++Input;  }
        if(Output != OutputEnd) { if(Output->Hovered) hovering = false; ++Output; }
    }

    Node.Hovered  =  hovering; // Whether mouse is over node
    Node.Hovered &= !Graph->HoveredNode() || is_hovered; // Check if a node later in the draw order is hovered
    Node.Hovered &= !Graph->FocusedNode() || is_focus;   // Chech if another node is focused
    Node.Hovered &= !Graph->SelectRegionStart(); // Check for drag selection

	// Pop Scope
	G.Scope = ImNodeGraphScope_Graph;
	Graph->CurrentNode = nullptr;

	// fix up header width
	if(Node.Header())
	{
		Node.Header->ScreenBounds.Min.x = Node.ScreenBounds.Min.x;
		Node.Header->ScreenBounds.Max.x = Node.ScreenBounds.Max.x;
	}

	Input      = Node.InputPins.begin();
	Output     = Node.OutputPins.begin();
	float Y    = Node.Header->ScreenBounds.Max.y + Style.NodePadding * Camera.Scale;
	float InX  = Node.ScreenBounds.Min.x + Style.NodePadding * Camera.Scale;

	while(Input != InputEnd || Output != OutputEnd)
	{
		float Step = 0.0f;
		if(Input != InputEnd)
		{
			Input->Pos = { InX, Y };
			Step = ImMax(Step, Input->ScreenBounds.GetHeight());
			++Input;
		}

		if(Output != OutputEnd)
		{
			float OutX = InX + Width - Output->ScreenBounds.GetWidth();
			Output->Pos = { OutX, Y };
			Step = ImMax(Step, Output->ScreenBounds.GetHeight());
			++Output;
		}

		Y += Step + Style.ItemSpacing;
	}
}

void ImNodeGraph::BeginNodeHeader(const char *title, ImColor color, ImColor hovered, ImColor active)
{
    // Validate global state
    IM_ASSERT(GImNodeGraph != nullptr);

    // Validate Graph state
    ImNodeGraphContext&   G = *GImNodeGraph;
    ImNodeGraphData*  Graph =  G.CurrentGraph;
    IM_ASSERT(Graph != nullptr);

    // Validate node scope
    ImNodeData* Node = Graph->CurrentNode;
    IM_ASSERT(G.Scope == ImNodeGraphScope_Node && Node != nullptr); // Ensure we are in the scope of a node
    IM_ASSERT(Node->Header() == false); // Ensure there is only one header

    if(Node->Hovered) color = hovered;
    if(Node->Active)  color = active;

    // Setup header
    Node->Header = ImNodeHeaderData{
        Node
    ,   color
    ,   ImRect()
    };

    // Create group
    ImGui::BeginGroup();
    ImGui::PushID(title);

    // Push scope
    G.Scope = ImNodeGraphScope_NodeHeader;
}

void ImNodeGraph::BeginNodeHeader(int id, ImColor color, ImColor hovered, ImColor active)
{
	// Validate global state
	IM_ASSERT(GImNodeGraph != nullptr);

	// Validate Graph state
	ImNodeGraphContext&   G = *GImNodeGraph;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	IM_ASSERT(Graph != nullptr);

	// Validate node scope
	ImNodeData* Node = Graph->CurrentNode;
	IM_ASSERT(G.Scope == ImNodeGraphScope_Node && Node != nullptr); // Ensure we are in the scope of a node
	IM_ASSERT(Node->Header() == false); // Ensure there is only one header

	if(Node->Hovered) color = hovered;
    if(Node->Active)  color = active;

	// Setup header
	Node->Header = ImNodeHeaderData{
		Node
	,   color
	,   ImRect()
	};

	// Create group
    ImGui::BeginGroup();
    ImGui::PushID(id);

    // Push scope
	G.Scope = ImNodeGraphScope_NodeHeader;
}

void ImNodeGraph::EndNodeHeader()
{
	// Validate global state
	IM_ASSERT(GImNodeGraph != nullptr);

	// Validate Graph state
	ImNodeGraphContext&   G = *GImNodeGraph;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	IM_ASSERT(Graph != nullptr);

	// Validate node scope
	ImNodeData* Node = Graph->CurrentNode;
	IM_ASSERT(G.Scope == ImNodeGraphScope_NodeHeader && Node != nullptr); // Ensure we are in the scope of a node
	IM_ASSERT(Node->Header()); // Ensure the header is valid

	// End Group
    ImGui::PopID();
    ImGui::EndGroup();

    const ImNodeGraphStyle& Style = Graph->Style;
	const ImGraphCamera&   Camera = Graph->Camera;
	Node->Header->ScreenBounds = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
	Node->Header->ScreenBounds.Expand(Style.NodePadding * Camera.Scale);

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Style.NodePadding * Camera.Scale);

	G.Scope = ImNodeGraphScope_Node;
}

ImSet<ImGuiID>& ImNodeGraph::GetSelected()
{
    // Validate global state
    IM_ASSERT(GImNodeGraph != nullptr);

    // Validate Graph state
    ImNodeGraphContext&   G = *GImNodeGraph;
    ImNodeGraphData*  Graph =  G.CurrentGraph;
    IM_ASSERT(Graph != nullptr);

    return Graph->Selected;
}

ImSet<ImGuiID> & ImNodeGraph::GetSelected(const char *title)
{
    // Validate global state
    IM_ASSERT(GImNodeGraph != nullptr);

    // Validate Graph state
    ImNodeGraphContext&   G = *GImNodeGraph;
    ImNodeGraphData*  Graph = FindGraphByTitle(title);

    return Graph->Selected;
}

ImUserID ImNodeGraph::GetUserID(ImGuiID id)
{
    return GImNodeGraph->CurrentGraph->Nodes[id].UserID;
}

void ImNodeGraph::SetPinColors(const ImColor *colors)
{
	// Validate global state
	IM_ASSERT(GImNodeGraph != nullptr);

	ImNodeGraphContext&   G = *GImNodeGraph;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	IM_ASSERT(Graph != nullptr);

	Graph->Style.PinColors = colors;
}

bool ImNodeGraph::BeginPin(const char *title, ImPinType type, ImPinDirection direction, ImPinFlags flags)
{
    // Validate global state
    IM_ASSERT(GImNodeGraph != nullptr);

    // Validate Graph state
    ImNodeGraphContext&   G = *GImNodeGraph;
    ImNodeGraphData*  Graph =  G.CurrentGraph;
    IM_ASSERT(Graph != nullptr);

    // Validate node scope
    ImNodeData* Node = Graph->CurrentNode;
    IM_ASSERT(G.Scope == ImNodeGraphScope_Node && Node != nullptr); // Ensure we are in the scope of a node

    // Push the pin
    ImGuiID id = ImGui::GetCurrentWindow()->GetID(title);
    ImPinData& Pin = (direction ? Node->OutputPins[id] : Node->InputPins[id]);
    Graph->CurrentPin = &Pin;

    bool changed = false;
    changed |= !Pin.NewConnections.empty();
    changed |= !Pin.ErasedConnections.empty();

    Pin.BNewConnections    = false;
    Pin.BErasedConnections = false;

    // Setup pin
    Pin.Node          = Node->ID;
    Pin.ID            = id;
    Pin.UserID.String = title;
    Pin.Type          = type;
    Pin.Direction     = direction;
    Pin.Flags         = flags;

    // Setup ImGui Group
    ImGui::SetCursorScreenPos(Pin.Pos); // The first frame the node will be completely garbled
    ImGui::BeginGroup();
    ImGui::PushID(static_cast<int>(id));

    // Push Scope
    G.Scope = ImNodeGraphScope_Pin;

    if(!Pin.Direction)
    {
        PinHead(id, Pin);
        ImGui::SameLine();
    }
    else if(!(flags & ImPinFlags_NoPadding))
    {
        DummyPinHead(Pin); // Guess this counts as padding
        ImGui::SameLine();
    }

    return changed;
}

bool ImNodeGraph::BeginPin(int iid, ImPinType type, ImPinDirection direction, ImPinFlags flags)
{
	// Validate global state
	IM_ASSERT(GImNodeGraph != nullptr);


	// Validate Graph state
	ImNodeGraphContext&   G = *GImNodeGraph;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	IM_ASSERT(Graph != nullptr);

	// Validate node scope
	ImNodeData* Node = Graph->CurrentNode;
	IM_ASSERT(G.Scope == ImNodeGraphScope_Node && Node != nullptr); // Ensure we are in the scope of a node

	// Push the pin
    ImGuiID        id = ImGui::GetCurrentWindow()->GetID(iid);
	ImPinData&    Pin = (direction ? Node->OutputPins[id] : Node->InputPins[id]);
    Graph->CurrentPin = &Pin;

    bool changed = false;
    changed |= !Pin.NewConnections.empty();
    changed |= !Pin.ErasedConnections.empty();

    Pin.BNewConnections    = false;
    Pin.BErasedConnections = false;

    // Setup pin
    Pin.Node       = Node->ID;
    Pin.ID         = id;
    Pin.UserID.Int = iid;
    Pin.Type       = type;
    Pin.Direction  = direction;
    Pin.Flags      = flags;

	// Setup ImGui Group
    ImGui::SetCursorScreenPos(Pin.Pos); // The first frame the node will be completely garbled
    ImGui::BeginGroup();
    ImGui::PushID(static_cast<int>(id));

	// Push Scope
	G.Scope = ImNodeGraphScope_Pin;

	if(!Pin.Direction)
	{
		PinHead(id, Pin);
		ImGui::SameLine();
	}
	else if(!(flags & ImPinFlags_NoPadding))
	{
		DummyPinHead(Pin); // Guess this counts as padding
		ImGui::SameLine();
	}

    return changed;
}

void ImNodeGraph::EndPin()
{
	// Validate global state
	IM_ASSERT(GImNodeGraph != nullptr);

	// Validate Graph state
	ImNodeGraphContext&   G = *GImNodeGraph;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	IM_ASSERT(Graph != nullptr);

	// Validate pin scope
	ImPinData* Pin = Graph->CurrentPin;
    IM_ASSERT(G.Scope == ImNodeGraphScope_Pin && Pin != nullptr); // Ensure we are in the scope of a pin

	if(Pin->Direction)
	{
		ImGui::SameLine();
		PinHead(Pin->ID, *Pin);
	}

    ImGui::PopID();
    ImGui::EndGroup();

    Pin->ScreenBounds = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };

	// Pop Scope
	G.Scope = ImNodeGraphScope_Node;

    if(Pin->BNewConnections == false)    Pin->NewConnections.clear();
    if(Pin->BErasedConnections == false) Pin->ErasedConnections.clear();
}

bool ImNodeGraph::IsPinConnected()
{
    // Validate global state
    IM_ASSERT(GImNodeGraph != nullptr);

    // Validate Graph state
    ImNodeGraphContext&   G = *GImNodeGraph;
    ImNodeGraphData*  Graph =  G.CurrentGraph;
    IM_ASSERT(Graph != nullptr);

    // Validate pin scope
    ImPinData* Pin = Graph->CurrentPin;
    IM_ASSERT(G.Scope == ImNodeGraphScope_Pin && Pin != nullptr); // Ensure we are in the scope of a pin

    return Pin->Connections.empty() == false;
}

bool ImNodeGraph::IsPinConnected(ImPinPtr pin)
{
    // Validate global state
    IM_ASSERT(GImNodeGraph != nullptr);

    // Validate Graph state
    ImNodeGraphContext&   G = *GImNodeGraph;
    ImNodeGraphData*  Graph =  G.CurrentGraph;
    IM_ASSERT(Graph != nullptr);

    // Validate pin scope
    ImPinData& Pin = Graph->FindPin(pin);

    return Pin.Connections.empty() == false;
}

const ImVector<ImGuiID>& ImNodeGraph::GetConnections()
{
    // Validate global state
    IM_ASSERT(GImNodeGraph != nullptr);

    // Validate Graph state
    ImNodeGraphContext&   G = *GImNodeGraph;
    ImNodeGraphData*  Graph =  G.CurrentGraph;
    IM_ASSERT(Graph != nullptr);

    // Validate pin scope
    ImPinData* Pin = Graph->CurrentPin;
    IM_ASSERT(G.Scope == ImNodeGraphScope_Pin && Pin != nullptr); // Ensure we are in the scope of a pin

    return Pin->Connections;
}

const ImVector<ImGuiID>& ImNodeGraph::GetConnections(ImPinPtr pin)
{
    // Validate global state
    IM_ASSERT(GImNodeGraph != nullptr);

    // Validate Graph state
    ImNodeGraphContext&   G = *GImNodeGraph;
    ImNodeGraphData*  Graph =  G.CurrentGraph;
    IM_ASSERT(Graph != nullptr);

    ImPinData& Pin = Graph->FindPin(pin);
    return Pin.Connections;
}

const ImVector<ImPinPtr>& ImNodeGraph::GetNewConnections()
{
    // Validate global state
    IM_ASSERT(GImNodeGraph != nullptr);

    // Validate Graph state
    ImNodeGraphContext&   G = *GImNodeGraph;
    ImNodeGraphData*  Graph =  G.CurrentGraph;
    IM_ASSERT(Graph != nullptr);

    // Validate pin scope
    ImPinData* Pin = Graph->CurrentPin;
    IM_ASSERT(G.Scope == ImNodeGraphScope_Pin && Pin != nullptr); // Ensure we are in the scope of a pin

    return Pin->NewConnections;
}

const ImVector<ImPinPtr>& ImNodeGraph::GetErasedConnections()
{
    // Validate global state
    IM_ASSERT(GImNodeGraph != nullptr);

    // Validate Graph state
    ImNodeGraphContext&   G = *GImNodeGraph;
    ImNodeGraphData*  Graph =  G.CurrentGraph;
    IM_ASSERT(Graph != nullptr);

    // Validate pin scope
    ImPinData* Pin = Graph->CurrentPin;
    IM_ASSERT(G.Scope == ImNodeGraphScope_Pin && Pin != nullptr); // Ensure we are in the scope of a pin

    return Pin->ErasedConnections;
}

ImUserID ImNodeGraph::GetUserID(ImPinPtr ptr)
{
    ImNodeData& Node = GImNodeGraph->CurrentGraph->Nodes[ptr.Node];
    return ptr.Direction ? Node.OutputPins[ptr.Pin].UserID : Node.InputPins[ptr.Pin].UserID;
}

ImPinPtr ImNodeGraph::GetPinPtr()
{
    // Validate global state
    IM_ASSERT(GImNodeGraph != nullptr);

    // Validate Graph state
    ImNodeGraphContext&   G = *GImNodeGraph;
    ImNodeGraphData*  Graph =  G.CurrentGraph;
    IM_ASSERT(Graph != nullptr);

    // Validate pin scope
    return *Graph->CurrentPin;
}
