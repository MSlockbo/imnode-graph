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
		Camera.Position -= Ctx.IO.MouseDelta / Camera.Scale;
}

void ImNodeGraph::DrawGraph(ImNodeGraphData* Graph)
{
	ImDrawList&         DrawList = *ImGui::GetWindowDrawList();
	ImDrawListSplitter& Splitter = DrawList._Splitter;
	ImObjectPool<ImNodeData>& Nodes = Graph->Nodes;

	for(int i = 0; i < Nodes.Size(); ++i)
	{
		if(!Nodes(i)) continue;

		ImNodeData& Node = Nodes[i];
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
{ }

void ImNodeGraph::DrawNode(ImNodeData& Node)
{
	ImNodeGraphData*  Graph =  Node.Graph;
	ImNodeGraphStyle& Style = Graph->Style;
	ImGraphCamera&   Camera = Graph->Camera;

	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, Style.NodeOutlineThickness * Camera.Scale);
	ImGui::PushStyleColor(ImGuiCol_Border, Style.GetColorU32(ImNodeGraphColor_NodeOutline));

	ImGui::RenderFrame(
		Node.ScreenBounds.Min, Node.ScreenBounds.Max
	,   Style.GetColorU32(ImNodeGraphColor_NodeBackground), true, Style.NodeRounding * Camera.Scale);

	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
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
	, NodeOutlineThickness(1.0f)
	, NodeOutlineSelectedThickness(4.0f)

	, SelectRegionOutlineThickness(2.0f)

	, PinPadding(2.0f)
	, PinOutlineThickness(3.0f)

	, ConnectionThickness(2.0f)

	, Colors{ ImColor(0x000000FF) }
	, PinColors(nullptr)
{
	Colors[ImNodeGraphColor_GridBackground]     = ImColor(0x11, 0x11, 0x11);
	Colors[ImNodeGraphColor_GridPrimaryLines]   = ImColor(0x88, 0x88, 0x88);
	Colors[ImNodeGraphColor_GridSecondaryLines] = ImColor(0x44, 0x44, 0x44);

	Colors[ImNodeGraphColor_NodeBackground]      = ImColor(0x88, 0x88, 0x88);
	Colors[ImNodeGraphColor_NodeTitleColor]      = ImColor(0xCC, 0xCC, 0xCC);
	Colors[ImNodeGraphColor_NodeOutline]         = ImColor(0x33, 0x33, 0x33);
	Colors[ImNodeGraphColor_NodeOutlineSelected] = ImColor(0xEF, 0xAE, 0x4B);

	Colors[ImNodeGraphColor_PinBackground] = ImColor(0x22, 0x22, 0x22);
	Colors[ImNodeGraphColor_PinName]       = ImColor(0x22, 0x22, 0x22);

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

	ImGui::PopFont();
	ImGui::EndChild();

	// Update State
	G.CurrentGraph = nullptr;
	G.Scope        = ImNodeGraphScope_None;
}


// Node ----------------------------------------------------------------------------------------------------------------

void ImNodeGraph::BeginNode(ImGuiID id, ImVec2& pos)
{
	IM_ASSERT(GImGuiNodes != nullptr);

	// Validate State
	ImGuiContext&       Ctx = *ImGui::GetCurrentContext();
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

	if(Pin.Node == nullptr)
	{
		Pin.Node = Node;
		Pin.Type = type;
		Pin.Direction = direction;
		Pin.Flags = flags;
	}
	ImGui::PushID(static_cast<int>(id));

	ImGui::BeginGroup();
	ImGui::SetCursorScreenPos(Graph->CurrentPin->Pos); // The first frame the node will be completely garbled
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

	ImGui::EndGroup();
	ImGui::PopID();

	Pin->ScreenBounds = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
}
