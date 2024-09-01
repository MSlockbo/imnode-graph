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
#include <imgui-docking/imgui_internal.h>

// =====================================================================================================================
// Math
// =====================================================================================================================

#include "stdint.h"
#include "math.h"

template<typename T>
T ImIsPrime(T x)
{
    if(x <= 1)                   return false;
    if(x == 2     || x == 3)     return true;
    if(x % 2 == 0 || x % 3 == 0) return false;

    uint64_t limit = static_cast<uint64_t>(sqrt(static_cast<float>(x)));
    for(T i = 5; i <= limit; i += 6)
    {
        if(x % i == 0 || x % (i + 2) == 0) return false;
    }

    return true;
}

// =====================================================================================================================
// Type & Forward Definitions
// =====================================================================================================================


// Data Structures -----------------------------------------------------------------------------------------------------

struct ImNodeGraphContext;

struct ImUserID;

struct ImPinPtr;
struct ImPinConnection;

template<typename T> struct ImObjectPool;
template<typename T> struct ImOptional;


// Typedefs ------------------------------------------------------------------------------------------------------------

// Graph Types
using ImNodeGraphColor   = int;
using ImNodeGraphFlags   = int;

// Pin Types
using ImPinType      = int;
using ImPinFlags     = int;
using ImPinDirection = bool;

// Connections
using ImConnectionValidation = bool(*)(ImPinPtr, ImPinPtr);

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

/**
 * \brief Optional value, similar to std::optional
 * \tparam T Value Type
 */
template<typename T>
struct ImOptional
{
    using value_type      = T;
    using reference       = T&;
    using const_reference = const T&;

    T    Value;
    bool Set;

    ImOptional() : Set(false) { }
    ImOptional(const T& value) : Value(value), Set(true) { }
    ImOptional(const ImOptional&) = default;
    ImOptional(ImOptional&&)      = default;
    ~ImOptional()                 = default;

    ImOptional& operator=(const ImOptional& other) = default;
    ImOptional& operator=(ImOptional&& other)      = default;

    ImOptional& operator=(const T& value) { Value = value; Set = true; return *this; }
    ImOptional& operator=(T&& value)      { Value = value; Set = true; return *this; }

    bool operator==(const ImOptional &) const = default;
    bool operator==(const T& o)         const { return Set && Value == o; }

    operator       T&()       { IM_ASSERT(Set); return Value; }
    operator const T&() const { IM_ASSERT(Set); return Value; }

          T* operator->()       { IM_ASSERT(Set); return &Value; }
    const T* operator->() const { IM_ASSERT(Set); return &Value; }

    bool operator()() const { return Set; }

    void Reset() { Set = false; }
};

/**
 * \brief Equivalent to std::deque, may be used as a queue, stack, etc.
 * \tparam T Value type
 */
template<typename T>
struct ImDeque
{
    class iterator;
    struct Node;
    using  NodePtr = Node*;

    using value_type      = T;
    using reference       = T&;
    using const_reference = const T&;
    using pointer         = T*;
    using const_pointer   = const T*;
    using node_index      = int;

    struct Node
    {
        T       Value;
        NodePtr Next, Prev;
    };

    NodePtr First, Last;
    int     Size;

    ImDeque() : First(nullptr), Last(nullptr) { }
    ImDeque(const ImDeque&);
    ImDeque(ImDeque&&) = default;
    ~ImDeque();

    bool Empty() const { return First == nullptr; }

    void Clear();

    void PushFront(const T& v);
    void PushBack(const T& v);

    void PopFront();
    void PopBack();

    T&       Front()       { IM_ASSERT(First); return First->Value; }
    const T& Front() const { IM_ASSERT(First); return First->Value; }

    T&       Back()        { IM_ASSERT(Last);  return Last->Value; }
    const T& Back() const  { IM_ASSERT(Last);  return Last->Value; }
};


#ifndef IMSET_MIN_CAPACITY
#define IMSET_MIN_CAPACITY 7 // Should be prime and >= 5
#endif

template<typename T>
size_t ImHash(T v) noexcept;

template<>
inline size_t ImHash<uint64_t>(uint64_t x) noexcept
{
    x ^= x >> 33U;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33U;
    x *= UINT64_C(0xc4ceb9fe1a85ec53);
    x ^= x >> 33U;

    return x;
}

template<> inline size_t ImHash<int>(int x)         noexcept { return ImHash(static_cast<uint64_t>(x)); }
template<> inline size_t ImHash<ImGuiID>(ImGuiID x) noexcept { return ImHash(static_cast<uint64_t>(x)); }

template<>
inline size_t ImHash<const char*>(const char* str) noexcept
{
    return ImHash(ImHashStr(str));
}

/**
 * \brief Set of values, based on Robin Hood Hashing
 * \tparam T Value type
 */
template<typename T>
struct ImSet
{
    struct Node
    {
        T    Value;
        bool Set;
        int  PSL;

        Node() : Value(), Set(false), PSL(0) { }
    };

    class  iterator;

    using value_type      = T;
    using reference       = T&;
    using const_reference = const T&;
    using pointer         = T*;
    using const_pointer   = const T*;
    using iterator_type   = iterator;

    int   Size;
    int   Capacity;
    Node* Table;
    float LoadFactor;

    ImSet();
    ImSet(const ImSet&);
    ImSet(ImSet&&) = default;
    ~ImSet();

    void Clear();

    void Insert(const T& v);
    void Erase(const T& v);
    bool Contains(const T& v) { return _Find(v) != -1; }

    int _Find(const T& v);

    bool _CheckLoadFactor();
    void _IncreaseCapacity();

    int _NextIndex(int i) { return (i + 1)            % Capacity; }
    int _PrevIndex(int i) { return (i - 1 + Capacity) % Capacity; }

    // These operate on the principle of (6n +/- 1)
    int  _NextPrime(int x);
    int  _PrevPrime(int x);

    class iterator
    {
    public:
        iterator(ImSet* set, int idx);
        iterator(const iterator&) = default;
        iterator(iterator&&) = default;
        ~iterator() = default;

        iterator& operator++();
        iterator  operator++(int);

        bool operator==(const iterator& o) const = default;
        bool operator!=(const iterator& o) const = default;

        const_reference operator*()  const { return  set_->Table[idx_].Value; }
        const_pointer   operator->() const { return &set_->Table[idx_].Value; }

    private:
        ImSet* set_;
        int    idx_;
    };

    iterator begin() { return iterator(this, 0);        }
    iterator end()   { return iterator(this, Capacity); }
};

/**
 * \brief Set of values, based on a Red-Black Tree
 * \tparam T Value type
 */
template<typename T>
struct ImOrderedSet
{
    class  iterator;
    struct Node;

    using  NodePtr   = Node*;
    using  NodeColor = bool;

    using value_type      = T;
    using reference       = T&;
    using const_reference = const T&;
    using pointer         = T*;
    using const_pointer   = const T*;
    using node_index      = int;
    using iterator_type   = iterator;

    enum NodeColor_
    {
        NodeColor_Red   = true
    ,   NodeColor_Black = false
    };

    struct Node
    {
        T         Value;
        NodeColor Color;
        NodePtr   Parent, Left, Right;

        Node(const T& v, NodePtr p) : Value(v), Color(NodeColor_Red), Parent(p), Left(nullptr), Right(nullptr) { }
    };

    Node* Root;
    int   Size;

    ImOrderedSet() : Root(nullptr), Size(0) { }
    ImOrderedSet(const ImOrderedSet&) = default;
    ImOrderedSet(ImOrderedSet&&) = default;
    ~ImOrderedSet();

    ImOrderedSet& operator=(const ImOrderedSet&)  = default;
    ImOrderedSet& operator=(ImOrderedSet&& other) = default;

    void Clear();
    void Insert(const T& v);
    void Erase(const T& v);

    Node* _Find(const T& v);

    static NodePtr _LeftMost(NodePtr x);
    void           _RotateLeft(NodePtr x);
    void           _RotateRight(NodePtr x);

    NodePtr _InsertBST(const T& v);
    void    _FixInsert(NodePtr x);

    NodePtr _EraseBST(const T& v);
    void    _FixErase(NodePtr x);

    class iterator
    {
    public:
        iterator(Node* node) : current_(node), visit_queue_() { }
        iterator(const iterator&) = default;
        iterator(iterator&&) = default;
        ~iterator() = default;

        iterator& operator++();
        iterator  operator++(int);

        bool operator==(const iterator& o) const { return current_ == o.current_; }
        bool operator!=(const iterator& o) const { return current_ != o.current_; }

        const_reference operator*()  const { return  current_->Value; }
        const_pointer   operator->() const { return &current_->Value; }

    private:
        NodePtr          current_;
        ImDeque<NodePtr> visit_queue_;
    };

    iterator begin() { return iterator(_LeftMost(Root)); }
    iterator end()   { return iterator(nullptr); }
};

/**
 * \brief Data Structure for holding a pool of objects with generated ids
 * \tparam T Value Type
 */
template<typename T>
struct ImObjectList
{
    class iterator;

    using value_type      = T;
    using reference       = T&;
    using const_reference = const T&;
    using pointer         = T*;
    using const_pointer   = const T*;
    using iterator_type   = iterator;

    ImVector<T>       Data;
    ImVector<bool>    Active;
	ImVector<ImGuiID> Freed;

    ImObjectList() = default;
    ImObjectList(const ImObjectList&) = default;
    ImObjectList(ImObjectList&&) = default;
    ~ImObjectList() = default;

	[[nodiscard]] inline size_t Size() const { return Active.Size; }

    ImGuiID Insert(const T& v);
    void    Erase(ImGuiID id);

    void Clear() { Data.clear(); Active.clear(); Freed.clear(); }
    void Reset() { memset(Active.Data, false, Active.size_in_bytes()); }
    void Cleanup();

    T& operator[](ImGuiID id) { IM_ASSERT(Active[id]); return Data[id]; }
    const T& operator[](ImGuiID id) const { IM_ASSERT(Active[id]); return Data[id]; }
    bool operator()(ImGuiID id) const { return Active[id]; }

    class iterator
    {
    public:
        iterator(ImObjectList& pool, int idx);

        iterator& operator++();
        iterator  operator++(int);

        bool operator==(const iterator&) const = default;
        bool operator!=(const iterator&) const = default;

        reference operator*()  const { return (*pool_)[idx_]; }
        pointer   operator->() const { return &(*pool_)[idx_]; }

    private:
        ImObjectList* pool_;
        int           idx_;
    };

    iterator begin() { return iterator(*this, 0); }
    iterator end()   { return iterator(*this, Active.size()); }
};

/**
 * \brief Data Structure for holding a pool of objects with user provided ids
 * \tparam T Value Type
 */
template<typename T>
struct ImObjectPool
{
    class iterator;
    class reverse_iterator;

	static constexpr int nullidx = -1;
	using value_type      = T;
    using reference       = T&;
    using const_reference = const T&;
    using pointer         = T*;
    using const_pointer   = const T*;
    using iterator_type   = iterator;

	ImVector<T>       Data;
	ImVector<bool>    Active;
	ImVector<ImGuiID> IdxToID;
	ImVector<int>     Freed;
    ImVector<int>     Order;
	ImGuiStorage      IDToIdx;

	ImObjectPool() = default;
	ImObjectPool(const ImObjectPool&) = default;
	ImObjectPool(ImObjectPool&&) = default;
	~ImObjectPool() = default;

	[[nodiscard]] inline size_t Size() const { return Order.Size; }

	void Clear() { Data.clear(); Active.clear(); Freed.clear(); IDToIdx.Clear(); IdxToID.clear(); }
	void Reset() { memset(Active.Data, false, Active.size_in_bytes()); }
	int  Cleanup();
	void PushToTop(ImGuiID id);

    ImObjectPool& operator=(const ImObjectPool&) = default;

	T& operator[](ImGuiID id);
	const T& operator[](ImGuiID id) const { int idx = IDToIdx.GetInt(id, nullidx); IM_ASSERT(idx != nullidx); return Data[idx]; };
	bool operator()(ImGuiID id) const { int idx = IDToIdx.GetInt(id, nullidx); return idx != nullidx && Active[idx]; }

	T& operator[](int idx)             { IM_ASSERT(idx >= 0 && idx < Data.Size); return Data[Order[idx]]; }
	const T& operator[](int idx) const { IM_ASSERT(idx >= 0 && idx < Data.Size); return Data[Order[idx]]; }
	bool operator()(int idx) const     { IM_ASSERT(idx >= 0 && idx < Data.Size); return Active[Order[idx]]; }

    class iterator
	{
	public:
	    iterator(ImObjectPool& pool, int idx);

	    iterator& operator++();
	    iterator  operator++(int);

	    bool operator==(const iterator&) const = default;
	    bool operator!=(const iterator&) const = default;

	    reference operator*()  const { return (*pool_)[idx_]; }
	    pointer   operator->() const { return &(*pool_)[idx_]; }

	private:
	    ImObjectPool* pool_;
	    int           idx_;
	};

    iterator begin() { return iterator(*this, 0); }
    iterator end()   { return iterator(*this, Order.size()); }

    class reverse_iterator
    {
    public:
        reverse_iterator(ImObjectPool& pool, int idx);

        reverse_iterator& operator++();
        reverse_iterator  operator++(int);

        bool operator==(const reverse_iterator&) const = default;
        bool operator!=(const reverse_iterator&) const = default;

        reference operator*()  const { return (*pool_)[idx_ - 1]; }
        pointer   operator->() const { return &(*pool_)[idx_ - 1]; }

    private:
        ImObjectPool* pool_;
        int           idx_;
    };

    reverse_iterator rbegin() { return reverse_iterator(*this, Order.size()); }
    reverse_iterator rend()   { return reverse_iterator(*this, 0); }


private:
	int _GetNextIndex(ImGuiID id);
	void _PushBack(ImGuiID id);
};

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

struct ImUserID
{
    const char* String;
    int         Int;

    ImUserID() : String(nullptr), Int(0) { }
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

    bool operator==(const ImPinPtr&) const = default;
};

struct ImPinConnection
{
	ImPinPtr A, B;
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

    void BeginGraphPostOp(const char* title);
    void EndGraphPostOp();

    void SetGraphValidation(ImConnectionValidation validation);

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

    ImSet<ImGuiID>& GetSelected();
    ImSet<ImGuiID>& GetSelected(const char* title);

    ImUserID GetUserID(ImGuiID id);


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
	bool BeginPin(const char* title, ImPinType type, ImPinDirection direction, ImPinFlags flags = 0);
	bool BeginPin(int id, ImPinType type, ImPinDirection direction, ImPinFlags flags = 0);
	void EndPin();

    bool                      IsPinConnected();
    bool                      IsPinConnected(ImPinPtr pin);
    const ImVector<ImGuiID>&  GetConnections();
    const ImVector<ImGuiID>&  GetConnections(ImPinPtr pin);
    const ImVector<ImPinPtr>& GetNewConnections();
    const ImVector<ImPinPtr>& GetErasedConnections();
    ImUserID                  GetUserID(ImPinPtr ptr);
    ImPinPtr                  GetPinPtr();


// Connections ---------------------------------------------------------------------------------------------------------

    bool             MakeConnection(const ImPinPtr& a, const ImPinPtr& b);
    void             BreakConnection(ImGuiID connection);
    void             BreakConnections(const ImPinPtr& pin);
}

// =====================================================================================================================
// Template Implementations
// =====================================================================================================================


// ImDeque -------------------------------------------------------------------------------------------------------------

template<typename T>
ImDeque<T>::~ImDeque()
{
    Clear();
}

template<typename T>
void ImDeque<T>::Clear()
{
    while(First)
    {
        NodePtr x = First;
        First = First->Next;
        delete x;
    }

    First = Last = nullptr;
    Size = 0;
}

template<typename T>
void ImDeque<T>::PushFront(const T &v)
{
    First = new Node{ v, First, nullptr };
    if(Last == nullptr) Last = First;
    ++Size;
}

template<typename T>
void ImDeque<T>::PushBack(const T &v)
{
    Last = new Node{ v, nullptr, Last };
    if(First == nullptr) First = Last;
    ++Size;
}

template<typename T>
void ImDeque<T>::PopFront()
{
    Node* erase = First;
    First = First->Next;
    if(First) First->Prev = nullptr;
    else      Last        = nullptr;
    delete erase;
    --Size;
}

template<typename T>
void ImDeque<T>::PopBack()
{
    Node* erase = Last;
    Last = Last->Prev;
    if(Last) Last->Prev = nullptr;
    else     First      = nullptr;
    delete erase;
    --Size;
}


// ImSet --------------------------------------------------------------------------------------------------------

template<typename T>
ImSet<T>::ImSet()
    : Table(nullptr)
    , Size(0)
    , Capacity(0)
    , LoadFactor(0.8f)
{

}

template<typename T>
ImSet<T>::ImSet(const ImSet& o)
    : Table(nullptr)
    , Size(o.Size)
    , Capacity(o.Capacity)
    , LoadFactor(o.LoadFactor)
{
    Table = IM_ALLOC(Capacity * sizeof(Node));
    memcpy(Table, o.Table, Capacity);
}

template<typename T>
ImSet<T>::~ImSet()
{
    Clear();
}

template<typename T>
void ImSet<T>::Clear()
{
    if(Table) IM_FREE(Table);
    Table = nullptr;
    Size = Capacity = 0;
}

template<typename T>
void ImSet<T>::Insert(const T &v)
{
    if(_CheckLoadFactor()) _IncreaseCapacity();

    int idx = ImHash(v) % Capacity;
    int PSL = 0;
    T Value = v;

    while(Table[idx].Set)
    {
        Node& node = Table[idx];
        if(Table[idx].Value == v) return;
        if(PSL > node.PSL) // Higher PSL, Swap
        {
            ImSwap(PSL, node.PSL);
            ImSwap(Value, node.Value);
        }
        idx = _NextIndex(idx);
        ++PSL;
    }

    Table[idx].Value = Value;
    Table[idx].Set   = true;
    Table[idx].PSL   = PSL;
    ++Size;
}

template<typename T>
void ImSet<T>::Erase(const T &v)
{
    int idx = _Find(v);
    if(idx == -1) return;

    Table[idx].Set = false;
    --Size;

    int prev = idx; idx = _NextIndex(idx);
    while(Table[idx].Set && Table[idx].PSL > 0)
    {
        Node &a = Table[prev], &b = Table[idx];
        ImSwap(a, b);
        --a.PSL; prev = idx; idx = _NextIndex(idx);
    }
}

template<typename T>
int ImSet<T>::_Find(const T &v)
{
    // Can be improved, not necessary for the needs of this library
    if(Capacity == 0) return -1;

    int idx = ImHash(v) % Capacity;
    int PSL = 0;

    while(Table[idx].Set)
    {
        Node& node = Table[idx];

        if(node.PSL > PSL) return -1;
        if(node.Value == v) return idx;

        idx = _NextIndex(idx); ++PSL;
    }

    return -1;
}

template<typename T>
bool ImSet<T>::_CheckLoadFactor()
{
    if(Capacity == 0) return true;
    float load = Size / static_cast<float>(Capacity);
    return load >= LoadFactor;
}

template<typename T>
void ImSet<T>::_IncreaseCapacity()
{
    Node* old = Table;
    int old_capacity = Capacity;
    Capacity = _NextPrime(Capacity);
    Table = static_cast<Node*>(IM_ALLOC(Capacity * sizeof(Node)));
    memset(Table, 0, Capacity * sizeof(Node));
    Size = 0;

    for(int i = 0; i < old_capacity; ++i)
    {
        if(old[i].Set) Insert(old[i].Value);
    }

    IM_FREE(old);
}

template<typename T>
int ImSet<T>::_NextPrime(int x)
{
    int n = (x + 1) / 6;
    n *= 2;

    while(true)
    {
        x = (n * 6) - 1;
        if(!ImIsPrime(x)) x = (n * 6) + 1;
        if(!ImIsPrime(x)) { ++n; continue; }
        return x < IMSET_MIN_CAPACITY ? IMSET_MIN_CAPACITY : x;
    }
}

template<typename T>
int ImSet<T>::_PrevPrime(int x)
{
    int n = (x + 1) / 6;
    n /= 2;

    while(true)
    {
        x = (n * 6) - 1;
        if(!ImIsPrime(x)) x = (n * 6) + 1;
        if(!ImIsPrime(x)) { --n; continue; }
        return x < IMSET_MIN_CAPACITY ? IMSET_MIN_CAPACITY : x;
    }
}

template<typename T>
ImSet<T>::iterator::iterator(ImSet* set, int idx)
    : set_(set), idx_(idx)
{
    while(idx_ < set_->Capacity && set_->Table[idx_].Set == false)
    {
        ++idx_;
    }
}

template<typename T>
typename ImSet<T>::iterator & ImSet<T>::iterator::operator++()
{
    ++idx_;

    while(idx_ < set_->Capacity && set_->Table[idx_].Set == false)
    {
        ++idx_;
    }

    return *this;
}

template<typename T>
typename ImSet<T>::iterator ImSet<T>::iterator::operator++(int)
{
    iterator ret = *this;
    ++idx_;

    while(idx_ < set_->Capacity && set_->Table[idx_].Set == false)
    {
        ++idx_;
    }

    return ret;
}


// ImOrderedSet --------------------------------------------------------------------------------------------------------

template<typename T>
ImOrderedSet<T>::~ImOrderedSet()
{
    Clear();
}

template<typename T>
void ImOrderedSet<T>::Clear()
{
    ImDeque<NodePtr> queue;
    queue.PushBack(Root);

    while(queue.Empty() == false)
    {
        NodePtr x = queue.Front(); queue.PopFront();
        if(x->Left)  queue.PushBack(x->Left);
        if(x->Right) queue.PushBack(x->Right);
        delete x;
    }

    Root = nullptr;
}

template<typename T>
void ImOrderedSet<T>::Insert(const T& value)
{
    NodePtr node = _InsertBST(value);
    if(node) _FixInsert(node);
}

template<typename T>
void ImOrderedSet<T>::Erase(const T &v)
{
}

template<typename T>
typename ImOrderedSet<T>::Node* ImOrderedSet<T>::_Find(const T &v)
{
    NodePtr x = Root;

    while(x)
    {
        if(v < x->Value)      x = x->Left;
        else if(x->Value < v) x = x->Right;
        else                  return x;
    }

    return x;
}

template<typename T>
ImOrderedSet<T>::NodePtr ImOrderedSet<T>::_LeftMost(NodePtr x)
{
    if(x == nullptr) return nullptr;
    while(x->Left) x = x->Left;
    return x;
}

template<typename T>
void ImOrderedSet<T>::_RotateLeft(NodePtr x)
{
    NodePtr y = x->Right;
    x->Right  = y->Left;

    if(y->Left) y->Left->Parent = x;

    y->Parent = x->Parent;

    if(x->Parent == nullptr)      Root = y;
    else if(x == x->Parent->Left) x->Parent->Left  = y;
    else                          x->Parent->Right = y;

    y->Left   = x;
    x->Parent = y;
}

template<typename T>
void ImOrderedSet<T>::_RotateRight(NodePtr x)
{
    NodePtr y = x->Left;
    x->Left   = y->Right;

    if(y->Right) y->Right->Parent = x;

    y->Parent = x->Parent;

    if(x->Parent == nullptr)       Root = y;
    else if(x == x->Parent->Right) x->Parent->Right = y;
    else                           x->Parent->Left  = y;

    y->Right  = x;
    x->Parent = y;
}

template<typename T>
typename ImOrderedSet<T>::NodePtr ImOrderedSet<T>::_InsertBST(const T &v)
{
    NodePtr *x = &Root;
    NodePtr  p = nullptr;

    while(*x)
    {
        NodePtr n = *x;

        if(v < n->Value)      x = &n->Left;
        else if(n->Value < v) x = &n->Right;
        else                  return nullptr;

        p = n;
    }

    return *x = new Node(v, p);
}

template<typename T>
void ImOrderedSet<T>::_FixInsert(NodePtr x)
{
    while (x != Root && x->Parent->Color == NodeColor_Red)
    {
        NodePtr p = x->Parent;
        NodePtr g = p->Parent;
        bool    s = p == g->Left;
        NodePtr u = s ? g->Right : g->Left;

        if (u->Color == NodeColor_Red)
        {
            p->Color = NodeColor_Black;
            u->Color = NodeColor_Black;
            g->Color = NodeColor_Red;
            x = g;
        }
        else if(s) // Best for readability and reducing the number of branches
        {
            if (x == p->Right)
            {
                x = x->Parent;
                _RotateLeft(x);
            }
            x->Parent->Color = NodeColor_Black;
            x->Parent->Parent->Color = NodeColor_Red;
            _RotateRight(x->Parent->Parent);
        }
        else
        {
            if (x == p->Left)
            {
                x = x->Parent;
                _RotateRight(x);
            }
            x->Parent->Color = NodeColor_Black;
            x->Parent->Parent->Color = NodeColor_Red;
            _RotateLeft(x->Parent->Parent);
        }
    }
    Root->Color = NodeColor_Black;
}

template<typename T>
typename ImOrderedSet<T>::NodePtr ImOrderedSet<T>::_EraseBST(const T &v)
{
    NodePtr x = _Find(v);
}

template<typename T>
typename ImOrderedSet<T>::iterator & ImOrderedSet<T>::iterator::operator++()
{
    NodePtr x = current_;
    NodePtr p = x->Parent;

    if(p && x == p->Left) visit_queue_.PushBack(p);
    if(x->Right) visit_queue_.PushBack(_LeftMost(x->Right));

    if(visit_queue_.Empty()) current_ = nullptr;
    else { current_ = visit_queue_.Back(); visit_queue_.PopBack(); }

    return *this;
}

template<typename T>
typename ImOrderedSet<T>::iterator ImOrderedSet<T>::iterator::operator++(int)
{
    iterator ret = *this;
    NodePtr x = current_;
    NodePtr p = x->Parent;

    if(p && x == p->Left) visit_queue_.PushBack(p);
    if(x->Right) visit_queue_.PushBack(_LeftMost(x->Right));

    if(visit_queue_.Empty()) current_ = nullptr;
    else { current_ = visit_queue_.Back(); visit_queue_.PopBack(); }

    return ret;
}


// ImObjectList --------------------------------------------------------------------------------------------------------

template<typename T>
ImGuiID ImObjectList<T>::Insert(const T &v)
{
    if(Freed.empty())
    {
        Data.push_back(v); Active.push_back(true);
        return Data.Size - 1;
    }

    ImGuiID id = Freed.back(); Freed.pop_back();
    Data[id] = v; Active[id] = true;
    return id;
}

template<typename T>
void ImObjectList<T>::Erase(ImGuiID id)
{
    Active[id] = false; Freed.push_back(id);
    Data[id] = T();
}

template<typename T>
void ImObjectList<T>::Cleanup()
{
    Freed.Size = 0;
    for(int i = 0; i < Active.Size; ++i)
    {
        if(Active[i]) continue;
        Freed.push_back(i);
    }
}

template<typename T>
ImObjectList<T>::iterator::iterator(ImObjectList &pool, int idx)
    : pool_(&pool)
    , idx_(idx)
{
    while(idx_ < pool_->Size() && !(*pool_)(idx_)) ++idx_;
}

template<typename T>
typename ImObjectList<T>::iterator & ImObjectList<T>::iterator::operator++()
{
    ++idx_;
    while(idx_ < pool_->Size() && !(*pool_)(idx_)) ++idx_;
    return *this;
}

template<typename T>
typename ImObjectList<T>::iterator ImObjectList<T>::iterator::operator++(int)
{
    iterator retval = *this;
    ++idx_;
    while(idx_ < pool_->Size() && !(*pool_)(idx_)) ++idx_;
    return retval;
}


// ImObjectPool --------------------------------------------------------------------------------------------------------

template<typename T>
int ImObjectPool<T>::Cleanup()
{
    int cnt = Freed.Size;
	Freed.Size = 0;
	for(int i = 0; i < Active.Size; ++i)
	{
		if(Active[i]) continue;

		Freed.push_back(i);
	    Order.find_erase(i);
	    IDToIdx.SetInt(IdxToID[i], nullidx);
	    IdxToID[i] = 0;
	}
    return Freed.Size - cnt;
}

template<typename T>
T& ImObjectPool<T>::operator[](ImGuiID id)
{
	int idx = IDToIdx.GetInt(id, nullidx); // Get the mapped index
	if(idx == nullidx)
	{
	    idx = _GetNextIndex(id); // If it is unassigned, get the next available index
	    Order.push_back(idx);
	}

	Active[idx] = true;
	return Data[idx];
}

template<typename T>
void ImObjectPool<T>::PushToTop(ImGuiID id) // Should always be O(n)
{
	int idx = IDToIdx.GetInt(id, nullidx);
	if(idx == nullidx) return;

    for(int i = Order.find_index(idx); i < Order.size() - 1; ++i)
    {
        ImSwap(Order[i], Order[i + 1]);
    }
}

template<typename T>
int ImObjectPool<T>::_GetNextIndex(ImGuiID id)
{
	int idx = Data.Size;                                   // Default to size of data array
	if(!Freed.empty())                                     // If there are freed indices, pop one
	{
		idx = Freed.back(); Freed.pop_back();
		Data[idx] = T(); Active[idx] = true; IdxToID[idx] = id; // Reset index values
	}
	else _PushBack(id); // Otherwise, push back new index
	IDToIdx.SetInt(id, idx);
	return idx;
}

template<typename T>
void ImObjectPool<T>::_PushBack(ImGuiID id)
{
	Data.push_back(T());
	Active.push_back(true);
	IdxToID.push_back(id);
}

template<typename T>
ImObjectPool<T>::iterator::iterator(ImObjectPool &pool, int idx)
    : pool_(&pool)
    , idx_(idx)
{
    while(idx_ < pool_->Size() && !(*pool_)(idx_)) ++idx_;
}

template<typename T>
typename ImObjectPool<T>::iterator & ImObjectPool<T>::iterator::operator++()
{
    ++idx_;
    while(idx_ < pool_->Size() && !(*pool_)(idx_)) ++idx_;
    return *this;
}

template<typename T>
typename ImObjectPool<T>::iterator ImObjectPool<T>::iterator::operator++(int)
{
    iterator retval = *this;
    ++idx_;
    while(idx_ < pool_->Size() && !(*pool_)(idx_)) ++idx_;
    return retval;
}

template<typename T>
ImObjectPool<T>::reverse_iterator::reverse_iterator(ImObjectPool &pool, int idx)
    : pool_(&pool)
    , idx_(idx)
{
    while(idx_ > 0 && !(*pool_)(idx_ - 1)) --idx_;
}

template<typename T>
typename ImObjectPool<T>::reverse_iterator & ImObjectPool<T>::reverse_iterator::operator++()
{
    --idx_;
    while(idx_ > 0 && !(*pool_)(idx_ - 1)) --idx_;
    return *this;
}

template<typename T>
typename ImObjectPool<T>::reverse_iterator ImObjectPool<T>::reverse_iterator::operator++(int)
{
    iterator retval = *this;
    --idx_;
    while(idx_ > 0 && !(*pool_)(idx_ - 1)) --idx_;
    return retval;
}

#endif //IMGUI_NODES_H
