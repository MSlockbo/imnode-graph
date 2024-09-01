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

#ifndef IMGUI_NODES_INTERNAL_H
#define IMGUI_NODES_INTERNAL_H

#include "imnode_graph.h"

// =====================================================================================================================
// Type & Forward Definitions
// =====================================================================================================================

// Typedefs
using ImNodeGraphScope = int;

// Data Structures
struct ImNodeGraphContext;
struct ImNodeGraphData;
struct ImNodeData;
struct ImNodeHeaderData;
struct ImPinData;
struct ImRect;

// =====================================================================================================================
// Enums
// =====================================================================================================================

enum ImNodeGraphScope_
{
	ImNodeGraphScope_None = 0
,   ImNodeGraphScope_Graph
,   ImNodeGraphScope_Node
,   ImNodeGraphScope_NodeHeader
,   ImNodeGraphScope_Pin
};


// =====================================================================================================================
// Math
// =====================================================================================================================

bool ImAABB(const ImRect& a, const ImRect& b);

// =====================================================================================================================
// Data Structures
// =====================================================================================================================

/**
 * \brief Context for node graph system
 */
struct ImNodeGraphContext
{
    bool                       Initialized;
	ImVector<ImFont*>          Fonts;
	ImNodeGraphScope           Scope;

	ImVector<ImNodeGraphData*> Graphs;
	ImGuiStorage               GraphsById;
	ImNodeGraphData*           CurrentGraph;

    ImNodeGraphContext();
};

/**
 * \brief Data structure for a graph instance
 */
struct ImNodeGraphData
{
    // Context & Style Vars
    ImNodeGraphContext*  Ctx;
    ImNodeGraphFlags     Flags;
    char*                Name;
    ImGuiID              ID;
	ImNodeGraphStyle     Style;
	ImNodeGraphSettings  Settings;
    ImVec2               Pos, Size;

    // Camera Vars
    ImGraphCamera        Camera;
    float                TargetZoom;
    bool                 IsPanning;

    // Input Vars
    ImOptional<ImVec2>   SelectRegionStart;
    ImSet<ImGuiID>       SelectRegion;
    ImVec2               DragOffset;
    bool                 Dragging, LockSelectRegion;

    // Node & Pin Vars
	ImObjectPool<ImNodeData> Nodes;
    ImOptional<ImGuiID>      HoveredNode, FocusedNode;
    ImOptional<ImPinPtr>     HoveredPin,  FocusedPin;
    ImSet<ImGuiID>           Selected;
    ImNodeData*              CurrentNode;
	ImPinData*               CurrentPin;
    int                      SubmitCount;

    // Connections
    ImOptional<ImPinPtr>          NewConnection; // For dragging pins
    ImObjectList<ImPinConnection> Connections;
    ImConnectionValidation        Validation;

    ImNodeGraphData(ImNodeGraphContext* ctx, const char* name);

    ImPinData& FindPin(ImPinPtr pin);
    [[nodiscard]] ImVec2 GetCenter() const { return Pos + Size * 0.5; }

    ImRect GetSelection();

    void UpdateSelection(ImGuiID node, bool allow_clear = false, bool removal = false);

    operator ImGuiID() const { return ID; }
};

struct ImNodeHeaderData
{
    ImNodeData* Node;
    ImColor     Color;
    ImRect      ScreenBounds;

    ImNodeHeaderData() : Node(nullptr), Color(0, 0, 0, 0), ScreenBounds(0, 0, 0, 0) { }
    ImNodeHeaderData(ImNodeData* node, ImColor color, ImRect screen_bounds) : Node(node), Color(color), ScreenBounds(screen_bounds) { }
    ImNodeHeaderData(const ImNodeHeaderData&) = default;

    ImNodeHeaderData& operator=(const ImNodeHeaderData&) = default;
};

/**
 * \brief Data structure for nodes
 */
struct ImNodeData
{
    ImNodeGraphData* Graph;
    ImGuiID          ID;
    ImUserID         UserID;
    ImVec2           Root;
    ImRect           ScreenBounds;
    int              BgChannelIndex, FgChannelIndex;
    bool             Hovered, Active;
    ImVec2           DragOffset;
    ImGuiID          PrevActiveItem, ActiveItem;

    ImOptional<ImNodeHeaderData> Header;
    ImObjectPool<ImPinData>      InputPins;
	ImObjectPool<ImPinData>      OutputPins;

	ImNodeData();
	ImNodeData(const ImNodeData&);
    ~ImNodeData() = default;

	ImNodeData& operator=(const ImNodeData&);

    operator ImGuiID() const { return ID; }
};

/**
 * \brief Data structure for pins
 */
struct ImPinData
{
    // Pin Info
    ImGuiID            Node;
    ImGuiID            ID;
    ImUserID           UserID;
    ImPinType          Type;
    ImPinDirection     Direction;
    ImPinFlags         Flags;
	ImVec2             Pos, Center;
	ImRect             ScreenBounds;
    ImVector<ImGuiID>  Connections;
    ImVector<ImPinPtr> NewConnections,  ErasedConnections;
    bool               BNewConnections, BErasedConnections;

    // Input
    bool Hovered;

	ImPinData();
	ImPinData(const ImPinData&) = default;
	~ImPinData() = default;

	ImPinData& operator=(const ImPinData&) = default;

    operator ImPinPtr() const { return { Node, ID, Direction }; }
};

// =====================================================================================================================
// Functionality
// =====================================================================================================================

// Extensions
namespace ImGui
{
    bool IsAnyModKeyDown();
}

namespace ImNodeGraph
{
// Context -------------------------------------------------------------------------------------------------------------

    void Initialize();
    void Shutdown();
    void LoadFonts();
    void LoadDefaultFont();

// Graph ---------------------------------------------------------------------------------------------------------------

	ImNodeGraphData* FindGraphByID(ImGuiID id);
	ImNodeGraphData* FindGraphByTitle(const char* title);
	ImNodeGraphData* CreateNewGraph(const char* title);

    void             DrawGrid(const ImRect& grid_bounds);
    void             GraphBehaviour(const ImRect& grid_bounds);
    void             DrawGraph(ImNodeGraphData* graph);
    void             DrawSelection(ImNodeGraphData* graph);

    int              PushChannels(int count);
    void             SetChannel(ImGuiID id);
    void             SwapChannel(ImDrawChannel& a, ImDrawChannel& b);
    void             SortChannels();

    bool             CheckConnectionValidity(ImGuiID id, ImPinConnection& connection);
    void             CleanupConnection(ImGuiID id, ImPinConnection& connection);

// Nodes ---------------------------------------------------------------------------------------------------------------

    void             DrawNode(ImNodeData& node);
    bool             NodeBehaviour(ImNodeData& node);

// Pins ----------------------------------------------------------------------------------------------------------------

    void             PinHead(ImGuiID id, ImPinData& pin);
    void             DummyPinHead(ImPinData& pin);

// Connections ---------------------------------------------------------------------------------------------------------

    void             BeginConnection(const ImPinPtr &pin);

    void             DrawConnection(const ImVec2& out, const ImVec4& out_col, const ImVec2& in, const ImVec4& in_col);
    void             DrawConnection(const ImPinData& pin, const ImVec2 &point);
    void             DrawConnection(const ImPinData& a, const ImPinData& b);

    ImVec2           PinConnectionAnchor(const ImPinData &a);

    void             AddPolylineMultiColored(ImDrawList& draw_list, const ImVec2 *points, int num_points,
                                             const ImVec4& c1, const ImVec4& c2, ImDrawFlags flags, float thickness);

    void             AddBezierCubicMultiColored(ImDrawList& draw_list, const ImVec2& p1, const ImVec2& p2,
                                                const ImVec2& p3, const ImVec2& p4, const ImVec4 &c1, const ImVec4 &c2, float thickness,
                                                int num_segments = 0);

    inline void      PathStrokeMultiColored(ImDrawList& draw_list, const ImVec4& c1, const ImVec4& c2,
                                            ImDrawFlags flags = 0, float thickness = 1.0f)
    { AddPolylineMultiColored(draw_list, draw_list._Path.Data, draw_list._Path.Size, c1, c2, flags, thickness); draw_list._Path.Size = 0; }
}

#endif //IMGUI_NODES_INTERNAL_H
