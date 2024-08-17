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

#ifndef IMGUI_NODES_H
#define IMGUI_NODES_H

#include <imgui-docking/imgui.h>

// =====================================================================================================================
// Type & Forward Definitions
// =====================================================================================================================

// Typedefs ------------------------------------------------------------------------------------------------------------

// Graph Types
using ImNodeGraphColor   = int;
using ImNodeGraphFlags   = int;

// Pin Types
using ImPinType      = int;
using ImPinFlags     = int;
using ImPinDirection = bool;


// Data Structures -----------------------------------------------------------------------------------------------------

struct ImNodeGraphContext;

struct ImPinPtr;
struct ImPinConnection;

// =====================================================================================================================
// Enums
// =====================================================================================================================

enum ImNodeGraphFlags_
{
	ImNodeGraphFlags_None     = 0
,   ImNodeGraphFlags_NoHeader = 1 << 0
};

enum ImNodeGraphColor_
{
    ImNodeGraphColor_GridBackground
,	ImNodeGraphColor_GridPrimaryLines
,	ImNodeGraphColor_GridSecondaryLines

,	ImNodeGraphColor_NodeBackground
,	ImNodeGraphColor_NodeHoveredBackground
,	ImNodeGraphColor_NodeActiveBackground
,	ImNodeGraphColor_NodeHeaderColor
,	ImNodeGraphColor_NodeHeaderHoveredColor
,	ImNodeGraphColor_NodeHeaderActiveColor
,	ImNodeGraphColor_NodeOutline
,	ImNodeGraphColor_NodeOutlineSelected

,	ImNodeGraphColor_PinBackground

,	ImNodeGraphColor_SelectRegionBackground
,	ImNodeGraphColor_SelectRegionOutline

,	ImNodeGraphColor_COUNT
};

enum ImPinDirection_
{
	ImPinDirection_Input  = false
,   ImPinDirection_Output = true
};

enum ImPinFlags_
{
	ImPinFlags_None = 0
,   ImPinFlags_NoPadding = 1
};

// =====================================================================================================================
// Data Structures
// =====================================================================================================================

struct ImGraphCamera
{
	ImVec2 Position;
	float  Scale;

    ImGraphCamera();
};

struct ImNodeGraphStyle
{
	float GridPrimaryStep;
	float GridPrimaryThickness;
	float GridSecondaryThickness;

	float NodeRounding;
	float NodePadding;
	float NodeOutlineThickness;
	float NodeOutlineSelectedThickness;

    float SelectRegionRounding;
    float SelectRegionOutlineThickness;

    float ItemSpacing;
    float PinRadius;
	float PinOutlineThickness;

    float ConnectionThickness;

	ImColor        Colors[ImNodeGraphColor_COUNT];
	const ImColor* PinColors;

	ImNodeGraphStyle();
	ImNodeGraphStyle(const ImNodeGraphStyle&) = default;

    ImU32  GetColorU32(ImNodeGraphColor idx)  const { return Colors[idx]; }
    ImVec4 GetColorVec4(ImNodeGraphColor idx) const { return Colors[idx]; }
};

struct ImNodeGraphSettings
{
	float  ZoomRate;
	float  ZoomSmoothing;
    ImVec2 ZoomBounds;

    ImNodeGraphSettings();
};

struct ImPinPtr
{
	ImGuiID        Node;
	ImGuiID        Pin;
    ImPinDirection Direction;

	bool operator<(const ImPinPtr& o) const
	{
		return Pin < o.Pin || Node < o.Node || Direction < o.Direction;
	}

    bool operator==(const ImPinPtr&) const = default;
};

struct ImPinConnection
{
	ImPinPtr A, B;

	bool operator<(const ImPinConnection& o) const
	{
		return A < o.A || B < o.B;
	}
};

// =====================================================================================================================
// Functionality
// =====================================================================================================================

namespace ImNodeGraph
{
// Context -------------------------------------------------------------------------------------------------------------

	/**
	 * \brief Setup the ImNodeGraph Context, must be called after ImGui::CreateContext()
	 */
	ImNodeGraphContext* CreateContext();

	/**
	 * \brief Cleanup the ImNodeGraph Context, must be called before ImGui::DestroyContext()
	 */
	void                DestroyContext(ImNodeGraphContext* ctx = NULL);

	/**
	 * \brief Getter for the current context
	 * \return Pointer to the current context
	 */
	ImNodeGraphContext* GetCurrentContext();
    void                SetCurrentContext(ImNodeGraphContext* ctx);

    void                AddFont(const char* path, float size = 0, const ImWchar* glyph_ranges = nullptr);


// Graph ---------------------------------------------------------------------------------------------------------------

	/**
	 * \brief Push a new graph to a window
	 * \param title Title for the graph
	 * \param size_arg Size of the graph,
	 */
	void BeginGraph(const char* title, const ImVec2& size_arg = { 0, 0 });
	void EndGraph();

    float GetCameraScale();

    ImVec2 GridToWindow(const ImVec2& pos);
    ImVec2 WindowToScreen(const ImVec2& pos);
    ImVec2 GridToScreen(const ImVec2& pos);

    ImVec2 ScreenToGrid(const ImVec2& pos);
    ImVec2 ScreenToWindow(const ImVec2& pos);
    ImVec2 WindowToGrid(const ImVec2& pos);

    ImVec2 SnapToGrid(const ImVec2& pos);

    void PushItemWidth(float width);


// Nodes ---------------------------------------------------------------------------------------------------------------

	/**
	 * \brief Push a new node to add widgets to in a window
	 * \param title Title of the node
	 * \return False if the node is collapsed
	 */
	void BeginNode(const char* title, ImVec2& pos);
	void BeginNode(int id, ImVec2& pos);
	void EndNode();

    void BeginNodeHeader(const char* title, ImColor color, ImColor hovered, ImColor active);
    void BeginNodeHeader(int id, ImColor color, ImColor hovered, ImColor active);
    void EndNodeHeader();


// Pins ----------------------------------------------------------------------------------------------------------------

	/**
	 * \brief Set the colors attributed to each pin type
	 * \param colors Array of color values
	 */
	void SetPinColors(const ImColor* colors);


	/**
	 * \brief Add a pin to the node
	 * \param name Name of the pin
	 * \param type Type of the pin
	 * \param direction Direction of the pin
	 * \param userData Data associated with the node, used for attribute callback
	 */
	void BeginPin(const char* title, ImPinType type, ImPinDirection direction, ImPinFlags flags = 0);
	void BeginPin(int id, ImPinType type, ImPinDirection direction, ImPinFlags flags = 0);
	void EndPin();

    bool IsPinConnected();
}

#endif //IMGUI_NODES_H
