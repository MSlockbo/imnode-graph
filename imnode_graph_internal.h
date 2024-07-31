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
#include <imgui-docking/imgui_internal.h>

// =====================================================================================================================
// Type & Forward Definitions
// =====================================================================================================================

// Typedefs
using ImNodeGraphScope = int;

// Data Structures
struct ImNodeGraphContext;
struct ImNodeGraphData;
template<typename T> struct ImObjectPool;
struct ImNodeData;
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
,   ImNodeGraphScope_Pin
};


// =====================================================================================================================
// Data Structures
// =====================================================================================================================

/**
 * \brief Data Structure for holding a pool of objects
 */
template<typename T>
struct ImObjectPool
{
	static constexpr int nullidx = -1;
	using value_type = T;

	ImVector<T>       Data;
	ImVector<bool>    Active;
	ImVector<ImGuiID> ID;
	ImVector<int>     Freed;
	ImGuiStorage      Map;

	ImObjectPool() = default;
	ImObjectPool(const ImObjectPool&) = default;
	ImObjectPool(ImObjectPool&&) = default;
	~ImObjectPool() = default;

	[[nodiscard]] inline size_t Size() const { return Active.Size; }

	void Clear() { Data.clear(); Active.clear(); Freed.clear(); Map.Clear(); }
	void Reset() { memset(Active.Data, false, Active.size_in_bytes()); }
	void Cleanup();
	void PushToTop(ImGuiID id);

	T& operator[](ImGuiID id);
	const T& operator[](ImGuiID id) const { int idx = Map.GetInt(id, nullidx); IM_ASSERT(idx != nullidx); return Data[idx]; };
	bool operator()(ImGuiID id) const { int idx = Map.GetInt(id, nullidx); return idx != nullidx && Active[idx]; }

	T& operator[](int idx) { IM_ASSERT(idx >= 0 && idx < Data.Size); return Data[idx]; }
	const T& operator[](int idx) const { IM_ASSERT(idx >= 0 && idx < Data.Size); return Data[idx]; }
	bool operator()(int idx) const { IM_ASSERT(idx >= 0 && idx < Data.Size); return Active[idx]; }

private:
	int _GetNextIndex(ImGuiID id);
	void _PushBack(ImGuiID id);
};

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
    ImNodeGraphContext*  Ctx;
    ImNodeGraphFlags     Flags;
    char*                Name;
    ImGuiID              ID;
	ImNodeGraphStyle     Style;
	ImNodeGraphSettings  Settings;
    ImVec2               Pos, Size;

    ImGraphCamera        Camera;
    float                TargetZoom;
    bool                 IsPanning;

	ImObjectPool<ImNodeData> Nodes;
    ImNodeData*              CurrentNode;
	ImPinData*               CurrentPin;
    int                      SubmitCount;

    ImNodeGraphData(ImNodeGraphContext* ctx, const char* name);

    [[nodiscard]] ImVec2 GetCenter() const { return Pos + Size * 0.5; }
};

/**
 * \brief Data structure for nodes
 */
struct ImNodeData
{
    ImNodeGraphData* Graph;
    ImGuiID          ID;
    ImVec2           Root;
    ImRect           ScreenBounds;
    int              BgChannelIndex, FgChannelIndex;

    ImObjectPool<ImPinData> InputPins;
	ImObjectPool<ImPinData> OutputPins;

	ImNodeData();
	ImNodeData(const ImNodeData&);
    ~ImNodeData() = default;

	ImNodeData& operator=(const ImNodeData&);
};

/**
 * \brief Data structure for pins
 */
struct ImPinData
{
    ImNodeData*    Node;
    ImPinType      Type;
    ImPinDirection Direction;
    ImPinFlags     Flags;
	ImVec2         Pos;
	ImRect         ScreenBounds;

	ImPinData();
	ImPinData(const ImPinData&) = default;
	~ImPinData() = default;

	ImPinData& operator=(const ImPinData&) = default;
};

// =====================================================================================================================
// Functionality
// =====================================================================================================================

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

    int PushChannels(int count);
    void             SetChannel(ImGuiID id);
    void             SwapChannel(ImDrawChannel& a, ImDrawChannel& b);
    void             SortChannels();

    ImVec2           GridToWindow(const ImVec2& pos);
    ImVec2           WindowToScreen(const ImVec2& pos);
    ImVec2           GridToScreen(const ImVec2& pos);

    ImVec2           ScreenToGrid(const ImVec2& pos);
    ImVec2           ScreenToWindow(const ImVec2& pos);
    ImVec2           WindowToGrid(const ImVec2& pos);

// Nodes ---------------------------------------------------------------------------------------------------------------

    void             DrawNode(ImNodeData& node);
}

// =====================================================================================================================
// Template Implementations
// =====================================================================================================================

template<typename T>
void ImObjectPool<T>::Cleanup()
{
	Freed.Size = 0;
	for(int i = 0; i < Active.Size; ++i)
	{
		if(Active[i]) continue;
		Freed.push_back(i);
	}
}

template<typename T>
T& ImObjectPool<T>::operator[](ImGuiID id)
{
	int idx = Map.GetInt(id, -1);                               // Get the mapped index
	if(idx == nullidx) idx = _GetNextIndex(id); // If it is unassigned, get the next available index

	Active[idx] = true;
	return Data[idx];
}

template<typename T>
void ImObjectPool<T>::PushToTop(ImGuiID id) // Should always be O(n)
{
	int idx = Map.GetInt(id, -1);
	if(idx == nullidx) return;

	while(idx < Data.Size)
	{
		int next_idx = idx + 1;
		ImSwap(Data[idx], Data[next_idx]);
		ImSwap(Active[idx], Active[next_idx]);
		Map.SetInt(ID[next_idx], idx);
		++idx;
	}
	Map.SetInt(id, idx - 1);
}

template<typename T>
int ImObjectPool<T>::_GetNextIndex(ImGuiID id)
{
	int idx = Data.Size;                                   // Default to size of data array
	if(!Freed.empty())                                     // If there are freed indices, pop one
	{
		idx = Freed.back(); Freed.pop_back();
		Data[idx] = T(); Active[idx] = true; ID[idx] = id; // Reset index values
	}
	else _PushBack(id); // Otherwise, push back new index
	Map.SetInt(id, idx);
	return idx;
}

template<typename T>
void ImObjectPool<T>::_PushBack(ImGuiID id)
{
	Data.push_back(T());
	Active.push_back(true);
	ID.push_back(id);
}

#endif //IMGUI_NODES_INTERNAL_H
