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

	for(ImPinConnection& connection : Graph->Connections)
	{
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

	int chnl = Splitter._Current;
	int strt = Splitter._Channels.Size - Graph.SubmitCount * 2;
	int cnt  = Graph.SubmitCount * 2;

	auto& indices = Graph.Nodes;
	auto& arr = Splitter._Channels;
	ImVector<ImDrawChannel> temp; temp.reserve(cnt); temp.resize(cnt);

    Splitter.SetCurrentChannel(&DrawList, 0);

	for(int i = 0; i < indices.Size(); ++i)
	{
		if(!indices(i)) continue;

		const int swap_idx = strt + i * 2;
		ImNodeData& node = indices[i];
		SwapChannel(temp[node.BgChannelIndex - strt], arr[swap_idx]);
		SwapChannel(temp[node.FgChannelIndex - strt], arr[swap_idx + 1]);
	}

	for(int i = 0; i < temp.Size; ++i)
	{
	    SwapChannel(arr[strt + i], temp[i]);
	}
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


ImPinData::ImPinData()
	: Node(0)
	, ID(0)
	, Type(0)
	, Direction(ImPinDirection_Input)
	, Flags(ImPinFlags_None)
	, Hovered(false)
{ }

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

	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

bool ImNodeGraph::NodeBehaviour(ImNodeData& Node)
{
	ImNodeGraphData& Graph = *Node.Graph;

    bool is_focus   = Graph.FocusedNode == Node;

	//Node.Hovered  =  hovering; // Whether mouse is over node
    //Node.Hovered &= !Graph.HoveredNode() || is_hovered; // Check if a node later in the draw order is hovered
    //Node.Hovered &= !Graph.FocusedNode() || is_focus;   // Chech if another node is focused
    //Node.Hovered &= !Graph.SelectRegionStart(); // Check for drag selection

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
    bool pressed = false, filled = false;
    if(ImGui::IsWindowFocused())
    {
        Pin.Hovered = ImGui::IsMouseHoveringRect(check_bb.Min, check_bb.Max);
        pressed = (Pin.Hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left));
        filled = (Pin.Hovered || !Pin.Connections.empty() || Graph.NewConnection == Pin);

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

	if(pressed)
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

void ImNodeGraph::BeginConnection(const ImPinPtr &pin)
{
	ImNodeGraphContext&  G = *GImNodeGraph;
	ImNodeGraphData& Graph = *G.CurrentGraph;
	Graph.NewConnection = pin;
}

void ImNodeGraph::MakeConnection(const ImPinPtr &a, const ImPinPtr &b)
{
	if(a.Direction == b.Direction) return;
	if(a.Node == b.Node) return;

	ImNodeGraphContext&  G = *GImNodeGraph;
	ImNodeGraphData& Graph = *G.CurrentGraph;

	ImPinData& A = Graph.FindPin(a);
	ImPinData& B = Graph.FindPin(b);

	ImGuiID connId = Graph.Connections.Insert({ a, b });

	A.Connections.push_back(connId);
	B.Connections.push_back(connId);
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
	if (num_points < 2)
		return;

	const bool closed = (flags & ImDrawFlags_Closed) != 0;
	const ImVec2 opaque_uv = draw_list._Data->TexUvWhitePixel;
	const int count = closed ? num_points : num_points - 1; // The number of line segments we need to draw
	const bool thick_line = (thickness > draw_list._FringeScale);

	if (draw_list.Flags & ImDrawListFlags_AntiAliasedLines)
	{
		// Anti-aliased stroke
		const float AA_SIZE = draw_list._FringeScale;

		// Thicknesses <1.0 should behave like thickness 1.0
		thickness = ImMax(thickness, 1.0f);
		const int   integer_thickness    = static_cast<int>(thickness);
		const float fractional_thickness = thickness - static_cast<float>(integer_thickness);

		// Do we want to draw this line using a texture?
		// - For now, only draw integer-width lines using textures to avoid issues with the way scaling occurs, could be improved.
		// - If AA_SIZE is not 1.0f we cannot use the texture path.
		const bool use_texture = (draw_list.Flags & ImDrawListFlags_AntiAliasedLinesUseTex) && (integer_thickness < IM_DRAWLIST_TEX_LINES_WIDTH_MAX) && (fractional_thickness <= 0.00001f) && (AA_SIZE == 1.0f);

		// We should never hit this, because NewFrame() doesn't set ImDrawListFlags_AntiAliasedLinesUseTex unless ImFontAtlasFlags_NoBakedLines is off
		IM_ASSERT_PARANOID(!use_texture || !(_Data->Font->ContainerAtlas->Flags & ImFontAtlasFlags_NoBakedLines));

		const int idx_count = use_texture ? (count * 6) : (thick_line ? count * 18 : count * 12);
		const int vtx_count = use_texture ? (num_points * 2) : (thick_line ? num_points * 4 : num_points * 3);
		draw_list.PrimReserve(idx_count, vtx_count);

		// Temporary buffer
		// The first <points_count> items are normals at each line point, then after that there are either 2 or 4 temp points for each line point
		draw_list._Data->TempBuffer.reserve_discard(num_points * ((use_texture || !thick_line) ? 3 : 5));
		ImVec2* temp_normals = draw_list._Data->TempBuffer.Data;
		ImVec2* temp_points = temp_normals + num_points;

		// Calculate normals (tangents) for each line segment
		for (int i1 = 0; i1 < count; i1++)
		{
			const int i2 = (i1 + 1) == num_points ? 0 : i1 + 1;
			float dx = points[i2].x - points[i1].x;
			float dy = points[i2].y - points[i1].y;
			IM_NORMALIZE2F_OVER_ZERO(dx, dy);
			temp_normals[i1].x = dy;
			temp_normals[i1].y = -dx;
		}
		if (!closed)
			temp_normals[num_points - 1] = temp_normals[num_points - 2];

		// If we are drawing a one-pixel-wide line without a texture, or a textured line of any width, we only need 2 or 3 vertices per point
		if (use_texture || !thick_line)
		{
			// [PATH 1] Texture-based lines (thick or non-thick)
			// [PATH 2] Non texture-based lines (non-thick)

			// The width of the geometry we need to draw - this is essentially <thickness> pixels for the line itself, plus "one pixel" for AA.
			// - In the texture-based path, we don't use AA_SIZE here because the +1 is tied to the generated texture
			//   (see ImFontAtlasBuildRenderLinesTexData() function), and so alternate values won't work without changes to that code.
			// - In the non texture-based paths, we would allow AA_SIZE to potentially be != 1.0f with a patch (e.g. fringe_scale patch to
			//   allow scaling geometry while preserving one-screen-pixel AA fringe).
			const float half_draw_size = use_texture ? ((thickness * 0.5f) + 1) : AA_SIZE;

			// If line is not closed, the first and last points need to be generated differently as there are no normals to blend
			if (!closed)
			{
				temp_points[0] = points[0] + temp_normals[0] * half_draw_size;
				temp_points[1] = points[0] - temp_normals[0] * half_draw_size;
				temp_points[(num_points-1)*2+0] = points[num_points-1] + temp_normals[num_points-1] * half_draw_size;
				temp_points[(num_points-1)*2+1] = points[num_points-1] - temp_normals[num_points-1] * half_draw_size;
			}

			// Generate the indices to form a number of triangles for each line segment, and the vertices for the line edges
			// This takes points n and n+1 and writes into n+1, with the first point in a closed line being generated from the final one (as n+1 wraps)
			// FIXME-OPT: Merge the different loops, possibly remove the temporary buffer.
			unsigned int idx1 = draw_list._VtxCurrentIdx; // Vertex index for start of line segment
			for (int i1 = 0; i1 < count; i1++) // i1 is the first point of the line segment
			{
				const int i2 = (i1 + 1) == num_points ? 0 : i1 + 1; // i2 is the second point of the line segment
				const unsigned int idx2 = ((i1 + 1) == num_points) ? draw_list._VtxCurrentIdx : (idx1 + (use_texture ? 2 : 3)); // Vertex index for end of segment

				// Average normals
				float dm_x = (temp_normals[i1].x + temp_normals[i2].x) * 0.5f;
				float dm_y = (temp_normals[i1].y + temp_normals[i2].y) * 0.5f;
				IM_FIXNORMAL2F(dm_x, dm_y);
				dm_x *= half_draw_size; // dm_x, dm_y are offset to the outer edge of the AA area
				dm_y *= half_draw_size;

				// Add temporary vertexes for the outer edges
				ImVec2* out_vtx = &temp_points[i2 * 2];
				out_vtx[0].x = points[i2].x + dm_x;
				out_vtx[0].y = points[i2].y + dm_y;
				out_vtx[1].x = points[i2].x - dm_x;
				out_vtx[1].y = points[i2].y - dm_y;

				if (use_texture)
				{
					// Add indices for two triangles
					draw_list._IdxWritePtr[0] = (ImDrawIdx)(idx2 + 0); draw_list._IdxWritePtr[1] = (ImDrawIdx)(idx1 + 0); draw_list._IdxWritePtr[2] = (ImDrawIdx)(idx1 + 1); // Right tri
					draw_list._IdxWritePtr[3] = (ImDrawIdx)(idx2 + 1); draw_list._IdxWritePtr[4] = (ImDrawIdx)(idx1 + 1); draw_list._IdxWritePtr[5] = (ImDrawIdx)(idx2 + 0); // Left tri
					draw_list._IdxWritePtr += 6;
				}
				else
				{
					// Add indexes for four triangles
					draw_list._IdxWritePtr[0] = (ImDrawIdx)(idx2 + 0); draw_list._IdxWritePtr[1]  = (ImDrawIdx)(idx1 + 0); draw_list._IdxWritePtr[2] = (ImDrawIdx)(idx1 + 2); // Right tri 1
					draw_list._IdxWritePtr[3] = (ImDrawIdx)(idx1 + 2); draw_list._IdxWritePtr[4]  = (ImDrawIdx)(idx2 + 2); draw_list._IdxWritePtr[5] = (ImDrawIdx)(idx2 + 0); // Right tri 2
					draw_list._IdxWritePtr[6] = (ImDrawIdx)(idx2 + 1); draw_list._IdxWritePtr[7]  = (ImDrawIdx)(idx1 + 1); draw_list._IdxWritePtr[8] = (ImDrawIdx)(idx1 + 0); // Left tri 1
					draw_list._IdxWritePtr[9] = (ImDrawIdx)(idx1 + 0); draw_list._IdxWritePtr[10] = (ImDrawIdx)(idx2 + 0); draw_list._IdxWritePtr[11] = (ImDrawIdx)(idx2 + 1); // Left tri 2
					draw_list._IdxWritePtr += 12;
				}

				idx1 = idx2;
			}

			// Add vertexes for each point on the line
			if (use_texture)
			{
				// If we're using textures we only need to emit the left/right edge vertices
				ImVec4 tex_uvs = draw_list._Data->TexUvLines[integer_thickness];
				/*if (fractional_thickness != 0.0f) // Currently always zero when use_texture==false!
				{
					const ImVec4 tex_uvs_1 = _Data->TexUvLines[integer_thickness + 1];
					tex_uvs.x = tex_uvs.x + (tex_uvs_1.x - tex_uvs.x) * fractional_thickness; // inlined ImLerp()
					tex_uvs.y = tex_uvs.y + (tex_uvs_1.y - tex_uvs.y) * fractional_thickness;
					tex_uvs.z = tex_uvs.z + (tex_uvs_1.z - tex_uvs.z) * fractional_thickness;
					tex_uvs.w = tex_uvs.w + (tex_uvs_1.w - tex_uvs.w) * fractional_thickness;
				}*/
				ImVec2 tex_uv0(tex_uvs.x, tex_uvs.y);
				ImVec2 tex_uv1(tex_uvs.z, tex_uvs.w);
				float  cstep = 1.0f / static_cast<float>(num_points);
				for (int i = 0; i < num_points; i++)
				{
					ImU32 col1 = ImGui::ColorConvertFloat4ToU32(ImLerp(c1, c2, cstep * (i + 0)));
					ImU32 col2 = ImGui::ColorConvertFloat4ToU32(ImLerp(c1, c2, cstep * (i + 1)));
					draw_list._VtxWritePtr[0].pos = temp_points[i * 2 + 0]; draw_list._VtxWritePtr[0].uv = tex_uv0; draw_list._VtxWritePtr[0].col = col1; // Left-side outer edge
					draw_list._VtxWritePtr[1].pos = temp_points[i * 2 + 1]; draw_list._VtxWritePtr[1].uv = tex_uv1; draw_list._VtxWritePtr[1].col = col2; // Right-side outer edge
					draw_list._VtxWritePtr += 2;
				}
			}
			else
			{
				// If we're not using a texture, we need the center vertex as well
				float  cstep = 1.0f / static_cast<float>(num_points);
				for (int i = 0; i < num_points; i++)
				{
					ImU32 col1 = ImGui::ColorConvertFloat4ToU32(ImLerp(c1, c2, cstep * (i + 0)));
					ImU32 col2 = ImGui::ColorConvertFloat4ToU32(ImLerp(c1, c2, cstep * (i + 1)));
					draw_list._VtxWritePtr[0].pos = points[i];              draw_list._VtxWritePtr[0].uv = opaque_uv; draw_list._VtxWritePtr[0].col = col1;       // Center of line
					draw_list._VtxWritePtr[1].pos = temp_points[i * 2 + 0]; draw_list._VtxWritePtr[1].uv = opaque_uv; draw_list._VtxWritePtr[1].col = col1 & ~IM_COL32_A_MASK; // Left-side outer edge
					draw_list._VtxWritePtr[2].pos = temp_points[i * 2 + 1]; draw_list._VtxWritePtr[2].uv = opaque_uv; draw_list._VtxWritePtr[2].col = col2 & ~IM_COL32_A_MASK; // Right-side outer edge
					draw_list._VtxWritePtr += 3;
				}
			}
		}
		else
		{
			// [PATH 2] Non texture-based lines (thick): we need to draw the solid line core and thus require four vertices per point
			const float half_inner_thickness = (thickness - AA_SIZE) * 0.5f;

			// If line is not closed, the first and last points need to be generated differently as there are no normals to blend
			if (!closed)
			{
				const int points_last = num_points - 1;
				temp_points[0] = points[0] + temp_normals[0] * (half_inner_thickness + AA_SIZE);
				temp_points[1] = points[0] + temp_normals[0] * (half_inner_thickness);
				temp_points[2] = points[0] - temp_normals[0] * (half_inner_thickness);
				temp_points[3] = points[0] - temp_normals[0] * (half_inner_thickness + AA_SIZE);
				temp_points[points_last * 4 + 0] = points[points_last] + temp_normals[points_last] * (half_inner_thickness + AA_SIZE);
				temp_points[points_last * 4 + 1] = points[points_last] + temp_normals[points_last] * (half_inner_thickness);
				temp_points[points_last * 4 + 2] = points[points_last] - temp_normals[points_last] * (half_inner_thickness);
				temp_points[points_last * 4 + 3] = points[points_last] - temp_normals[points_last] * (half_inner_thickness + AA_SIZE);
			}

			// Generate the indices to form a number of triangles for each line segment, and the vertices for the line edges
			// This takes points n and n+1 and writes into n+1, with the first point in a closed line being generated from the final one (as n+1 wraps)
			// FIXME-OPT: Merge the different loops, possibly remove the temporary buffer.
			unsigned int idx1 = draw_list._VtxCurrentIdx; // Vertex index for start of line segment
			for (int i1 = 0; i1 < count; i1++) // i1 is the first point of the line segment
			{
				const int i2 = (i1 + 1) == num_points ? 0 : (i1 + 1); // i2 is the second point of the line segment
				const unsigned int idx2 = (i1 + 1) == num_points ? num_points : (idx1 + 4); // Vertex index for end of segment

				// Average normals
				float dm_x = (temp_normals[i1].x + temp_normals[i2].x) * 0.5f;
				float dm_y = (temp_normals[i1].y + temp_normals[i2].y) * 0.5f;
				IM_FIXNORMAL2F(dm_x, dm_y);
				float dm_out_x = dm_x * (half_inner_thickness + AA_SIZE);
				float dm_out_y = dm_y * (half_inner_thickness + AA_SIZE);
				float dm_in_x = dm_x * half_inner_thickness;
				float dm_in_y = dm_y * half_inner_thickness;

				// Add temporary vertices
				ImVec2* out_vtx = &temp_points[i2 * 4];
				out_vtx[0].x = points[i2].x + dm_out_x;
				out_vtx[0].y = points[i2].y + dm_out_y;
				out_vtx[1].x = points[i2].x + dm_in_x;
				out_vtx[1].y = points[i2].y + dm_in_y;
				out_vtx[2].x = points[i2].x - dm_in_x;
				out_vtx[2].y = points[i2].y - dm_in_y;
				out_vtx[3].x = points[i2].x - dm_out_x;
				out_vtx[3].y = points[i2].y - dm_out_y;

				// Add indexes
				draw_list._IdxWritePtr[0]  = (ImDrawIdx)(idx2 + 1); draw_list._IdxWritePtr[1]  = (ImDrawIdx)(idx1 + 1); draw_list._IdxWritePtr[2]  = (ImDrawIdx)(idx1 + 2);
				draw_list._IdxWritePtr[3]  = (ImDrawIdx)(idx1 + 2); draw_list._IdxWritePtr[4]  = (ImDrawIdx)(idx2 + 2); draw_list._IdxWritePtr[5]  = (ImDrawIdx)(idx2 + 1);
				draw_list._IdxWritePtr[6]  = (ImDrawIdx)(idx2 + 1); draw_list._IdxWritePtr[7]  = (ImDrawIdx)(idx1 + 1); draw_list._IdxWritePtr[8]  = (ImDrawIdx)(idx1 + 0);
				draw_list._IdxWritePtr[9]  = (ImDrawIdx)(idx1 + 0); draw_list._IdxWritePtr[10] = (ImDrawIdx)(idx2 + 0); draw_list._IdxWritePtr[11] = (ImDrawIdx)(idx2 + 1);
				draw_list._IdxWritePtr[12] = (ImDrawIdx)(idx2 + 2); draw_list._IdxWritePtr[13] = (ImDrawIdx)(idx1 + 2); draw_list._IdxWritePtr[14] = (ImDrawIdx)(idx1 + 3);
				draw_list._IdxWritePtr[15] = (ImDrawIdx)(idx1 + 3); draw_list._IdxWritePtr[16] = (ImDrawIdx)(idx2 + 3); draw_list._IdxWritePtr[17] = (ImDrawIdx)(idx2 + 2);
				draw_list._IdxWritePtr += 18;

				idx1 = idx2;
			}

			// Add vertices
			float  cstep = 1.0f / static_cast<float>(num_points);
			for (int i = 0; i < num_points; i++)
			{
				ImU32 col1 = ImGui::ColorConvertFloat4ToU32(ImLerp(c1, c2, cstep * (i + 0)));
				ImU32 col2 = ImGui::ColorConvertFloat4ToU32(ImLerp(c1, c2, cstep * (i + 1)));
				draw_list._VtxWritePtr[0].pos = temp_points[i * 4 + 0]; draw_list._VtxWritePtr[0].uv = opaque_uv; draw_list._VtxWritePtr[0].col = col1 & ~IM_COL32_A_MASK;
				draw_list._VtxWritePtr[1].pos = temp_points[i * 4 + 1]; draw_list._VtxWritePtr[1].uv = opaque_uv; draw_list._VtxWritePtr[1].col = col1;
				draw_list._VtxWritePtr[2].pos = temp_points[i * 4 + 2]; draw_list._VtxWritePtr[2].uv = opaque_uv; draw_list._VtxWritePtr[2].col = col2;
				draw_list._VtxWritePtr[3].pos = temp_points[i * 4 + 3]; draw_list._VtxWritePtr[3].uv = opaque_uv; draw_list._VtxWritePtr[3].col = col2 & ~IM_COL32_A_MASK;
				draw_list._VtxWritePtr += 4;
			}
		}
		draw_list._VtxCurrentIdx += (ImDrawIdx)vtx_count;
	}
	else
	{
		// [PATH 4] Non texture-based, Non anti-aliased lines
		const int idx_count = count * 6;
		const int vtx_count = count * 4;    // FIXME-OPT: Not sharing edges
		draw_list.PrimReserve(idx_count, vtx_count);

		float  cstep = 1.0f / static_cast<float>(num_points);
		for (int i1 = 0; i1 < count; i1++)
		{
			const int i2 = (i1 + 1) == num_points ? 0 : i1 + 1;
			const ImVec2& p1 = points[i1];
			const ImVec2& p2 = points[i2];

			float dx = p2.x - p1.x;
			float dy = p2.y - p1.y;
			IM_NORMALIZE2F_OVER_ZERO(dx, dy);
			dx *= (thickness * 0.5f);
			dy *= (thickness * 0.5f);

			ImU32 col1 = ImGui::ColorConvertFloat4ToU32(ImLerp(c1, c2, cstep * (i1 + 0)));
			ImU32 col2 = ImGui::ColorConvertFloat4ToU32(ImLerp(c1, c2, cstep * (i1 + 1)));
			draw_list._VtxWritePtr[0].pos.x = p1.x + dy; draw_list._VtxWritePtr[0].pos.y = p1.y - dx; draw_list._VtxWritePtr[0].uv = opaque_uv; draw_list._VtxWritePtr[0].col = col1;
			draw_list._VtxWritePtr[1].pos.x = p2.x + dy; draw_list._VtxWritePtr[1].pos.y = p2.y - dx; draw_list._VtxWritePtr[1].uv = opaque_uv; draw_list._VtxWritePtr[1].col = col1;
			draw_list._VtxWritePtr[2].pos.x = p2.x - dy; draw_list._VtxWritePtr[2].pos.y = p2.y + dx; draw_list._VtxWritePtr[2].uv = opaque_uv; draw_list._VtxWritePtr[2].col = col2;
			draw_list._VtxWritePtr[3].pos.x = p1.x - dy; draw_list._VtxWritePtr[3].pos.y = p1.y + dx; draw_list._VtxWritePtr[3].uv = opaque_uv; draw_list._VtxWritePtr[3].col = col2;
			draw_list._VtxWritePtr += 4;

			draw_list._IdxWritePtr[0] = (ImDrawIdx)(draw_list._VtxCurrentIdx); draw_list._IdxWritePtr[1] = (ImDrawIdx)(draw_list._VtxCurrentIdx + 1); draw_list._IdxWritePtr[2] = (ImDrawIdx)(draw_list._VtxCurrentIdx + 2);
			draw_list._IdxWritePtr[3] = (ImDrawIdx)(draw_list._VtxCurrentIdx); draw_list._IdxWritePtr[4] = (ImDrawIdx)(draw_list._VtxCurrentIdx + 2); draw_list._IdxWritePtr[5] = (ImDrawIdx)(draw_list._VtxCurrentIdx + 3);
			draw_list._IdxWritePtr += 6;
			draw_list._VtxCurrentIdx += 4;
		}
	}
}

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

	// Reset nodes
	Graph->Nodes.Cleanup(); Graph->Nodes.Reset();

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

void ImNodeGraph::BeginNode(ImGuiID id, ImVec2& pos)
{
	IM_ASSERT(GImNodeGraph != nullptr);

	// Validate State
	ImNodeGraphContext&   G = *GImNodeGraph;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	IM_ASSERT(G.Scope == ImNodeGraphScope_Graph && Graph != nullptr); // Ensure we are in the scope of a graph

	// Get Node
	ImNodeData& Node = Graph->Nodes[id];
	if(Node.Graph == nullptr) { Node.Graph = Graph; Node.Root = pos; Node.ID = id; }

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

    bool hovering   = ImGui::IsItemHovered() && !other_hovered;
    bool is_focus   = Graph->FocusedNode == Node;
    bool is_hovered = Graph->HoveredNode == Node;

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

		if(Input  != InputEnd)  ++Input;
		if(Output != OutputEnd) ++Output;
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

void ImNodeGraph::BeginNodeHeader(ImGuiID id, ImColor color, ImColor hovered, ImColor active)
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
    ImGui::PushID(static_cast<int>(id));

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

void ImNodeGraph::SetPinColors(const ImColor *colors)
{
	// Validate global state
	IM_ASSERT(GImNodeGraph != nullptr);

	ImNodeGraphContext&   G = *GImNodeGraph;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	IM_ASSERT(Graph != nullptr);

	Graph->Style.PinColors = colors;
}

void ImNodeGraph::BeginPin(ImGuiID id, ImPinType type, ImPinDirection direction, ImPinFlags flags)
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
	ImPinData& Pin = (direction ? Node->OutputPins[id] : Node->InputPins[id]);
	Graph->CurrentPin = &Pin;

    // Setup pin
    Pin.Node = Node->ID;
    Pin.ID   = id;
    Pin.Type = type;
    Pin.Direction = direction;
    Pin.Flags = flags;

	// Setup ImGui Group
    ImGui::BeginGroup();
    ImGui::PushID(static_cast<int>(id));
    ImGui::SetCursorScreenPos(Pin.Pos); // The first frame the node will be completely garbled

	// Push Scope
	G.Scope = ImNodeGraphScope_Pin;

	if(!Pin.Direction)
	{
		PinHead(id, Pin);
		ImGui::SameLine();
	}
	else
	{
		DummyPinHead(Pin); // Guess this counts as padding
		ImGui::SameLine();
	}
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
}
