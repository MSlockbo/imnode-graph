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

struct ImNodeFontConfig
{
	char* Path;
	float Size;
	const ImWchar* GlyphRanges;
};

ImNodeGraphContext*   GImGuiNodes = nullptr;
ImVector<ImNodeFontConfig> GFonts;
float                GFontUpscale = 4.0f;

ImVec4 operator*(const ImVec4& v, float s) { return { v.x * s, v.y * s, v.z * s, v.w * s }; }

// =====================================================================================================================
// Internal Functionality
// =====================================================================================================================


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
	ImNodeGraphContext& G = *GImGuiNodes;
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
	ImNodeGraphContext& G = *GImGuiNodes;
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
	ImNodeGraphContext& G = *GImGuiNodes;

	bool first = true;
	for(const auto& font : GFonts)
	{
		ImFontConfig cfg = ImFontConfig();
		cfg.OversampleH = cfg.OversampleV = 1;
		cfg.SizePixels = font.Size * GFontUpscale;
		cfg.MergeMode = !first;
		cfg.PixelSnapH = true;
		G.Fonts.push_back(Ctx.IO.Fonts->AddFontFromFileTTF(font.Path, 0, &cfg, font.GlyphRanges));

		first = false;
	}
}

void ImNodeGraph::LoadDefaultFont()
{
	ImGuiContext&     Ctx = *ImGui::GetCurrentContext();
	ImNodeGraphContext& G = *GImGuiNodes;

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
	, FocusedPin(-1, -1, false)
	, SubmitCount(0)
{ Name = ImStrdup(name); }

ImNodeGraphData* ImNodeGraph::FindGraphByID(ImGuiID id)
{
	ImNodeGraphContext& G = *GImGuiNodes;
	return reinterpret_cast<ImNodeGraphData*>(G.GraphsById.GetVoidPtr(id));
}

ImNodeGraphData* ImNodeGraph::FindGraphByTitle(const char *title)
{
	ImGuiID id = ImHashStr(title);
	return FindGraphByID(id);
}

ImNodeGraphData* ImNodeGraph::CreateNewGraph(const char *title)
{
	ImNodeGraphContext& G = *GImGuiNodes;
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
	ImNodeGraphData&  Graph = *GImGuiNodes->CurrentGraph;
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
		,   Style.Colors[ImNodeGraphColor_GridSecondaryLines], Style.GridSecondaryThickness
		);
	}

	for(float y = GridStart.y; y < GridEnd.y; y += GridSecondaryStep)
	{
		DrawList.AddLine(
			{ 0, y }, { GridEnd.x, y }
		,   Style.Colors[ImNodeGraphColor_GridSecondaryLines], Style.GridSecondaryThickness
		);
	}

	// Primary Grid
	for(float x = GridStart.x; x < GridEnd.x; x += GridPrimaryStep)
	{
		DrawList.AddLine(
			{ x, 0 }, { x, GridEnd.y }
		,   Style.Colors[ImNodeGraphColor_GridPrimaryLines], Style.GridPrimaryThickness
		);
	}

	for(float y = GridStart.y; y < GridEnd.y; y += GridPrimaryStep)
	{
		DrawList.AddLine(
			{ 0, y }, { GridEnd.x, y }
		,   Style.Colors[ImNodeGraphColor_GridPrimaryLines], Style.GridPrimaryThickness
		);
	}
}

void ImNodeGraph::GraphBehaviour(const ImRect& grid_bounds)
{
	// Context
	ImGuiContext&             Ctx = *ImGui::GetCurrentContext();
	ImNodeGraphContext&         G = *GImGuiNodes;
	ImNodeGraphData&        Graph = *G.CurrentGraph;
	ImNodeGraphSettings& Settings = Graph.Settings;
	ImGraphCamera&         Camera = Graph.Camera;


	// Check Focus
	if(!ImGui::IsWindowFocused()) return;

	// Vars
	const bool Hovered = ImGui::IsMouseHoveringRect(grid_bounds.Min, grid_bounds.Max);

	// Zooming
	if(Hovered) Graph.TargetZoom += Ctx.IO.MouseWheel * Settings.ZoomRate * Camera.Scale;
	Graph.TargetZoom = ImClamp(Graph.TargetZoom, Settings.ZoomBounds.x, Settings.ZoomBounds.y);
	Camera.Scale     = ImLerp(Camera.Scale, Graph.TargetZoom, Ctx.IO.DeltaTime * Settings.ZoomSmoothing);

	// Item Focus
	if(ImGui::IsAnyItemFocused()) return;

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
}

void ImNodeGraph::DrawGraph(ImNodeGraphData* Graph)
{
	ImDrawList&         DrawList = *ImGui::GetWindowDrawList();
	ImDrawListSplitter& Splitter = DrawList._Splitter;
	ImObjectPool<ImNodeData>& Nodes = Graph->Nodes;

	for(ImNodeData& Node : Nodes)
	{
		SetChannel(Node.BgChannelIndex);
		DrawNode(Node);
	}

	SortChannels();

	Splitter.Merge(&DrawList);
}

ImVec2 ImNodeGraph::GridToWindow(const ImVec2 &pos)
{
	ImNodeGraphContext&   G = *GImGuiNodes;
	ImNodeGraphData&  Graph = *G.CurrentGraph;

	return GridToScreen(pos) - Graph.Pos;
}

ImVec2 ImNodeGraph::WindowToScreen(const ImVec2 &pos)
{
	ImNodeGraphContext&   G = *GImGuiNodes;
	ImNodeGraphData&  Graph = *G.CurrentGraph;

	return Graph.Pos + pos;
}

ImVec2 ImNodeGraph::GridToScreen(const ImVec2& pos)
{
	ImNodeGraphContext& G = *GImGuiNodes;
	ImNodeGraphData&  Graph = *G.CurrentGraph;
	ImGraphCamera&   Camera = Graph.Camera;

	return (pos - Camera.Position) * Camera.Scale + Graph.GetCenter();
}

ImVec2 ImNodeGraph::ScreenToGrid(const ImVec2& pos)
{
	ImNodeGraphContext& G = *GImGuiNodes;
	IM_ASSERT(G.CurrentGraph);

	ImNodeGraphData&  Graph = *G.CurrentGraph;
	ImGraphCamera&   Camera = Graph.Camera;
	return Camera.Position + (pos - Graph.GetCenter()) / Camera.Scale;
}

ImVec2 ImNodeGraph::ScreenToWindow(const ImVec2 &pos)
{
	ImNodeGraphContext&   G = *GImGuiNodes;
	ImNodeGraphData&  Graph = *G.CurrentGraph;

	return pos - Graph.Pos;
}

ImVec2 ImNodeGraph::WindowToGrid(const ImVec2 &pos)
{
	ImNodeGraphContext&   G = *GImGuiNodes;
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

	return Splitter._Channels.size() - count;
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
	ImNodeGraphContext&   G = *GImGuiNodes;
	ImNodeGraphData&  Graph = *G.CurrentGraph;
	ImDrawList&         DrawList = *ImGui::GetWindowDrawList();
	ImDrawListSplitter& Splitter = DrawList._Splitter;

	int chnl = Splitter._Current;
	int strt = Splitter._Channels.Size - Graph.SubmitCount * 2;
	int cnt  = Graph.SubmitCount * 2;

	auto& indices = Graph.Nodes;
	auto& arr = Splitter._Channels;
	ImVector<ImDrawChannel> temp; temp.reserve(cnt); temp.resize(cnt);

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

	Splitter.SetCurrentChannel(&DrawList, chnl);
}


// Nodes ---------------------------------------------------------------------------------------------------------------

ImNodeData::ImNodeData()
	: Graph(nullptr)
	, ID(0)
	, Root(0, 0)
	, ScreenBounds()
	, FgChannelIndex(0)
	, BgChannelIndex(0)
{

}

ImNodeData::ImNodeData(const ImNodeData& other)
	: Graph(other.Graph)
	, ID(other.ID)
	, Root(other.Root)
	, ScreenBounds(other.ScreenBounds)
	, FgChannelIndex(0)
	, BgChannelIndex(0)
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
	FgChannelIndex = 0;
	BgChannelIndex = 0;
	InputPins.Clear();
	OutputPins.Clear();

	return *this;
}


ImPinData::ImPinData()
	: Node(nullptr)
	, Type(0)
	, Direction(ImPinDirection_Input)
	, Flags(ImPinFlags_None)
	, Pos()
	, ScreenBounds()
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

	// Render Base Frame
	ImGui::RenderFrame(
		Node.ScreenBounds.Min, Node.ScreenBounds.Max
	,   Style.GetColorU32(ImNodeGraphColor_NodeBackground), true, Style.NodeRounding * Camera.Scale
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

	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

void ImNodeGraph::DrawPinHead(ImPinData& pin)
{
	ImNodeGraphContext&   G = *GImGuiNodes;
	ImNodeGraphData*  Graph =  G.CurrentGraph;

	const ImNodeGraphStyle& Style =  Graph->Style;
	const ImGraphCamera&   Camera =  Graph->Camera;
	ImGuiWindow&           Window = *ImGui::GetCurrentWindow();

	static const char format[] = "##pin%d";
	char res[16 + 1] = "";
	ImFormatString(res, IM_ARRAYSIZE(res), format, pin.ID);

    ImVec4 PinColor = Style.PinColors[pin.Type].Value;
    PinColor = PinColor * (pin.Hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left) ? 0.8f : 1.0f);

    if(Graph->FocusedPin == pin) ImGui::PushStyleColor(ImGuiCol_FrameBg, PinColor);
    else ImGui::PushStyleColor(ImGuiCol_FrameBg, Style.GetColorVec4(ImNodeGraphColor_PinBackground));

	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, PinColor);
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  PinColor);
	ImGui::PushStyleColor(ImGuiCol_Border,         PinColor);
	ImGui::PushStyleColor(ImGuiCol_CheckMark,      PinColor);
	ImGui::PushStyleColor(ImGuiCol_BorderShadow,   ImVec4(0, 0, 0, 0));

	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, Style.PinOutlineThickness * Camera.Scale);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    ImVec2{ Style.ItemSpacing, Style.ItemSpacing } * Camera.Scale);

	ImGui::RadioButton(res, false, Style.PinRadius * Camera.Scale);
	pin.Hovered = ImGui::IsItemHovered();

	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(6);
}

void ImNodeGraph::DummyPinHead(ImPinData &pin)
{
	ImNodeGraphContext&   G = *GImGuiNodes;
    ImNodeGraphData*  Graph =  G.CurrentGraph;
	const ImGraphCamera&   Camera =  Graph->Camera;
    const ImNodeGraphStyle& Style =  Graph->Style;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, Style.PinOutlineThickness * Camera.Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    ImVec2{ Style.ItemSpacing, Style.ItemSpacing } * Camera.Scale);

    ImGuiWindow& Window = *ImGui::GetCurrentWindow();
    ImGuiStyle&   IStyle = ImGui::GetStyle();
    const ImVec2 label_size = ImGui::CalcTextSize("##", NULL, true);

    const float square_sz = ImGui::GetFrameHeight();
    const ImVec2 pos = Window.DC.CursorPos;
    const ImRect total_bb(pos, pos + ImVec2(square_sz + (label_size.x > 0.0f ? IStyle.ItemInnerSpacing.x + label_size.x : 0.0f), label_size.y + IStyle.FramePadding.y * 2.0f));
    ImGui::ItemSize(total_bb, IStyle.FramePadding.y);
    ImGui::ItemAdd(total_bb, -1);
    ImGui::SameLine();

	ImGui::PopStyleVar(2);
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
	return GImGuiNodes;
}

void ImNodeGraph::SetCurrentContext(ImNodeGraphContext *ctx)
{
	GImGuiNodes = ctx;
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

	Colors[ImNodeGraphColor_NodeBackground]      = ImColor(0x88, 0x88, 0x88);
	Colors[ImNodeGraphColor_NodeOutline]         = ImColor(0x33, 0x33, 0x33);
	Colors[ImNodeGraphColor_NodeOutlineSelected] = ImColor(0xEF, 0xAE, 0x4B);

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
	IM_ASSERT(GImGuiNodes != nullptr);
	ImNodeGraphContext& G = *GImGuiNodes;

	// Ensure we are in the scope of a window
	ImGuiWindow* Window = ImGui::GetCurrentWindow();
	IM_ASSERT(Window != nullptr);                       // Ensure we are within a window

	// Validate parameters and graph state
	IM_ASSERT(title != nullptr && title[0] != '\0');    // Graph name required
	IM_ASSERT(G.Scope == ImNodeGraphScope_None);     // Ensure we are not in the scope of another graph

	// Get Graph
	ImNodeGraphData* Graph = FindGraphByTitle(title);
	const bool FirstFrame = (Graph == nullptr);
	if(FirstFrame) Graph = CreateNewGraph(title);

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

	// Reset nodes
	Graph->Nodes.Cleanup(); Graph->Nodes.Reset();

	// Begin the Graph Child
	ImGui::PushStyleColor(ImGuiCol_ChildBg, static_cast<ImU32>(Style.Colors[ImNodeGraphColor_GridBackground]));
	ImGui::BeginChild(title, Size, 0, ImGuiWindowFlags_NoScrollbar);
	ImGui::PopStyleColor();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2{ Style.ItemSpacing, Style.ItemSpacing } * Camera.Scale);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ Style.ItemSpacing, Style.ItemSpacing } * Camera.Scale);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ Style.NodePadding, Style.NodePadding } * Camera.Scale);
	DrawGrid({ Graph->Pos, Graph->Pos + Graph->Size });
}

void ImNodeGraph::EndGraph()
{
	// Validate global state
	IM_ASSERT(GImGuiNodes != nullptr);
	ImNodeGraphContext&  G = *GImGuiNodes;

	// Validate graph state
	ImNodeGraphData* Graph = G.CurrentGraph;
	IM_ASSERT(G.Scope == ImNodeGraphScope_Graph && Graph != nullptr); // Ensure we are in the scope of a graph

	DrawGraph(Graph);

	GraphBehaviour({ Graph->Pos, Graph->Pos + Graph->Size });

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
    IM_ASSERT(GImGuiNodes != nullptr);
    ImNodeGraphContext&  G = *GImGuiNodes;

    // Validate graph state
    ImNodeGraphData* Graph = G.CurrentGraph;
    IM_ASSERT(G.Scope != ImNodeGraphScope_None && Graph != nullptr); // Ensure we are in the scope of a graph

    return Graph->Camera.Scale;
}


// Node ----------------------------------------------------------------------------------------------------------------

void ImNodeGraph::BeginNode(ImGuiID id, ImVec2& pos)
{
	IM_ASSERT(GImGuiNodes != nullptr);

	// Validate State
	ImNodeGraphContext&   G = *GImGuiNodes;
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
	ImGui::PushID(static_cast<int>(id));
	ImGui::BeginGroup();
}

void ImNodeGraph::EndNode()
{
	// Validate global state
	IM_ASSERT(GImGuiNodes != nullptr);

	// Validate graph state
	ImNodeGraphContext&   G = *GImGuiNodes;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	IM_ASSERT(Graph != nullptr);
	IM_ASSERT(G.Scope == ImNodeGraphScope_Node && Graph->CurrentNode != nullptr); // Ensure we are in the scope of a node

	ImGui::EndGroup();
	ImGui::PopID();

	const ImNodeGraphStyle& Style = Graph->Style;
	const ImGraphCamera&   Camera = Graph->Camera;

	ImNodeData& Node = *Graph->CurrentNode;
	Node.ScreenBounds = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
	Node.ScreenBounds.Expand(Style.NodePadding * Camera.Scale);

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

void ImNodeGraph::BeginNodeHeader(ImGuiID id, ImColor color)
{
	// Validate global state
	IM_ASSERT(GImGuiNodes != nullptr);

	// Validate Graph state
	ImNodeGraphContext&   G = *GImGuiNodes;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	IM_ASSERT(Graph != nullptr);

	// Validate node scope
	ImNodeData* Node = Graph->CurrentNode;
	IM_ASSERT(G.Scope == ImNodeGraphScope_Node && Node != nullptr); // Ensure we are in the scope of a node
	IM_ASSERT(Node->Header() == false); // Ensure there is only one header

	// Setup header
	Node->Header = ImNodeHeaderData{
		Node
	,   color
	};

	// Create group
	ImGui::PushID(static_cast<int>(id));
	ImGui::BeginGroup();

	// Push scope
	G.Scope = ImNodeGraphScope_NodeHeader;
}

void ImNodeGraph::EndNodeHeader()
{
	// Validate global state
	IM_ASSERT(GImGuiNodes != nullptr);

	// Validate Graph state
	ImNodeGraphContext&   G = *GImGuiNodes;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	IM_ASSERT(Graph != nullptr);

	// Validate node scope
	ImNodeData* Node = Graph->CurrentNode;
	IM_ASSERT(G.Scope == ImNodeGraphScope_NodeHeader && Node != nullptr); // Ensure we are in the scope of a node
	IM_ASSERT(Node->Header()); // Ensure the header is valid

	// End Group
	ImGui::EndGroup();
	ImGui::PopID();

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
	IM_ASSERT(GImGuiNodes != nullptr);

	ImNodeGraphContext&   G = *GImGuiNodes;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	IM_ASSERT(Graph != nullptr);

	Graph->Style.PinColors = colors;
}

void ImNodeGraph::BeginPin(ImGuiID id, ImPinType type, ImPinDirection direction, ImPinFlags flags)
{
	// Validate global state
	IM_ASSERT(GImGuiNodes != nullptr);


	// Validate Graph state
	ImNodeGraphContext&   G = *GImGuiNodes;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	IM_ASSERT(Graph != nullptr);

	// Validate node scope
	ImNodeData* Node = Graph->CurrentNode;
	IM_ASSERT(G.Scope == ImNodeGraphScope_Node && Node != nullptr); // Ensure we are in the scope of a node

	// Push the pin
	ImPinData& Pin = (direction ? Node->OutputPins[id] : Node->InputPins[id]);
	Graph->CurrentPin = &Pin;

	// Setup pin on first frame
	if(Pin.Node == nullptr)
	{
		Pin.Node = Node;
		Pin.ID   = id;
		Pin.Type = type;
		Pin.Direction = direction;
		Pin.Flags = flags;
	}

	// Setup ImGui Group
	ImGui::PushID(static_cast<int>(id));
	ImGui::BeginGroup();
	ImGui::SetCursorScreenPos(Pin.Pos); // The first frame the node will be completely garbled

	// Push Scope
	G.Scope = ImNodeGraphScope_Pin;

	if(!Pin.Direction)
	{
	    DrawPinHead(Pin);
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
	IM_ASSERT(GImGuiNodes != nullptr);

	// Validate Graph state
	ImNodeGraphContext&   G = *GImGuiNodes;
	ImNodeGraphData*  Graph =  G.CurrentGraph;
	IM_ASSERT(Graph != nullptr);

	// Validate pin scope
	ImPinData* Pin = Graph->CurrentPin;
    IM_ASSERT(G.Scope == ImNodeGraphScope_Pin && Pin != nullptr); // Ensure we are in the scope of a pin

    if(Pin->Direction)
    {
        ImGui::SameLine();
        DrawPinHead(*Pin);
    }

	ImGui::EndGroup();
	ImGui::PopID();

	Pin->ScreenBounds = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };

	// Pop Scope
	G.Scope = ImNodeGraphScope_Node;
}


// =====================================================================================================================
// ImGui Extensions
// =====================================================================================================================


bool ImGui::RadioButton(const char *label, bool active, float radius)
{
	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	const ImGuiID id = window->GetID(label);
	const ImVec2 label_size = CalcTextSize(label, NULL, true);

	const float square_sz = GetFrameHeight();
	const ImVec2 pos = window->DC.CursorPos;
	const ImRect check_bb(pos, pos + ImVec2(square_sz, square_sz));
	const ImRect total_bb(pos, pos + ImVec2(square_sz + (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f), label_size.y + style.FramePadding.y * 2.0f));
	ItemSize(total_bb, style.FramePadding.y);
	if (!ItemAdd(total_bb, id))
		return false;

	ImVec2 center = check_bb.GetCenter();
	center.x = IM_ROUND(center.x);
	center.y = IM_ROUND(center.y);

	bool hovered, held;
	bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);
	if (pressed)
		MarkItemEdited(id);

	RenderNavHighlight(total_bb, id);
	const int num_segment = window->DrawList->_CalcCircleAutoSegmentCount(radius);
	window->DrawList->AddCircleFilled(center, radius, GetColorU32((held && hovered) ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), num_segment);
	if (active)
	{
		const float pad = ImMax(1.0f, IM_TRUNC(square_sz / 6.0f));
		window->DrawList->AddCircleFilled(center, radius - pad, GetColorU32(ImGuiCol_CheckMark));
	}

	if (style.FrameBorderSize > 0.0f)
	{
		window->DrawList->AddCircle(center + ImVec2(1, 1), radius, GetColorU32(ImGuiCol_BorderShadow), num_segment, style.FrameBorderSize);
		window->DrawList->AddCircle(center, radius, GetColorU32(ImGuiCol_Border), num_segment, style.FrameBorderSize);
	}

	ImVec2 label_pos = ImVec2(check_bb.Max.x + style.ItemInnerSpacing.x, check_bb.Min.y + style.FramePadding.y);
	if (g.LogEnabled)
		LogRenderedText(&label_pos, active ? "(x)" : "( )");
	if (label_size.x > 0.0f)
		RenderText(label_pos, label);

	IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
	return pressed;
}
