///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2012-2013 DreamWorks Animation LLC
//
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
//
// Redistributions of source code must retain the above copyright
// and license notice and the following restrictions and disclaimer.
//
// *     Neither the name of DreamWorks Animation nor the names of
// its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// IN NO EVENT SHALL THE COPYRIGHT HOLDERS' AND CONTRIBUTORS' AGGREGATE
// LIABILITY FOR ALL CLAIMS REGARDLESS OF THEIR BASIS EXCEED US$250.00.
//
///////////////////////////////////////////////////////////////////////////
//
/// @file InternalNode.h
///
/// @brief Internal table nodes for OpenVDB trees

#ifndef OPENVDB_TREE_INTERNALNODE_HAS_BEEN_INCLUDED
#define OPENVDB_TREE_INTERNALNODE_HAS_BEEN_INCLUDED

#include <boost/shared_array.hpp>
#include <boost/static_assert.hpp>
#include <openvdb/Platform.h>
#include <openvdb/util/NodeMasks.h>
#include <openvdb/io/Compression.h> // for io::readData(), etc.
#include <openvdb/math/Math.h> // for Abs(), isExactlyEqual()
#include <openvdb/version.h>
#include <openvdb/Types.h>
#include "Iterator.h"
#include "NodeUnion.h"
#include "Util.h"


namespace openvdb {
OPENVDB_USE_VERSION_NAMESPACE
namespace OPENVDB_VERSION_NAME {
namespace tree {

template<typename _ChildNodeType, Index Log2Dim>
class InternalNode
{
public:
    typedef _ChildNodeType                        ChildNodeType;
    typedef typename ChildNodeType::LeafNodeType  LeafNodeType;
    typedef typename ChildNodeType::ValueType     ValueType;
    typedef NodeUnion<ValueType, ChildNodeType>   UnionType;
    typedef util::NodeMask<Log2Dim>               NodeMaskType;

    static const Index
        LOG2DIM      = Log2Dim,
        TOTAL        = Log2Dim + ChildNodeType::TOTAL,
        DIM          = 1 << TOTAL,
        NUM_VALUES   = 1 << (3 * Log2Dim),
        LEVEL        = 1 + ChildNodeType::LEVEL; // level 0 = leaf
    static const Index64
        NUM_VOXELS   = uint64_t(1) << (3 * TOTAL); // total # of voxels represented by this node

    /// @brief ValueConverter<T>::Type is the type of an InternalNode having the same
    /// child hierarchy and dimensions as this node but a different value type, T.
    template<typename OtherValueType>
    struct ValueConverter {
        typedef InternalNode<typename ChildNodeType::template ValueConverter<
            OtherValueType>::Type, Log2Dim> Type;
    };


    InternalNode() {}

    explicit InternalNode(const ValueType& offValue);

    InternalNode(const Coord&, const ValueType& value, bool active = false);

    /// Deep copy constructor
    InternalNode(const InternalNode&);

    /// Topology copy constructor
    template<typename OtherChildNodeType>
    InternalNode(const InternalNode<OtherChildNodeType, Log2Dim>& other,
                 const ValueType& background, TopologyCopy);

    /// Topology copy constructor
    template<typename OtherChildNodeType>
    InternalNode(const InternalNode<OtherChildNodeType, Log2Dim>& other,
                 const ValueType& offValue, const ValueType& onValue,
                 TopologyCopy);

    virtual ~InternalNode();

protected:
    typedef typename NodeMaskType::OnIterator    MaskOnIterator;
    typedef typename NodeMaskType::OffIterator   MaskOffIterator;
    typedef typename NodeMaskType::DenseIterator MaskDenseIterator;

    // Type tags to disambiguate template instantiations
    struct ValueOn {}; struct ValueOff {}; struct ValueAll {};
    struct ChildOn {}; struct ChildOff {}; struct ChildAll {};

    // The following class templates implement the iterator interfaces specified in Iterator.h
    // by providing getItem() and setItem() methods for active values and/or inactive values.

    template<typename NodeT, typename ChildT, typename MaskIterT, typename TagT>
    struct ChildIter: public SparseIteratorBase<
        MaskIterT, ChildIter<NodeT, ChildT, MaskIterT, TagT>, NodeT, ChildT>
    {
        ChildIter() {}
        ChildIter(const MaskIterT& iter, NodeT* parent): SparseIteratorBase<
            MaskIterT, ChildIter<NodeT, ChildT, MaskIterT, TagT>, NodeT, ChildT>(iter, parent) {}

        ChildT& getItem(Index pos) const { return *(this->parent().getChildNode(pos)); }

        // Note: setItem() can't be called on const iterators.
        void setItem(Index pos, const ChildT& c) const { this->parent().setChildNode(pos, &c); }
    };// ChildIter

    template<typename NodeT, typename ValueT, typename MaskIterT, typename TagT>
    struct ValueIter: public SparseIteratorBase<
        MaskIterT, ValueIter<NodeT, ValueT, MaskIterT, TagT>, NodeT, ValueT>
    {
        ValueIter() {}
        ValueIter(const MaskIterT& iter, NodeT* parent): SparseIteratorBase<
            MaskIterT, ValueIter<NodeT, ValueT, MaskIterT, TagT>, NodeT, ValueT>(iter, parent) {}

        const ValueT& getItem(Index pos) const { return this->parent().mNodes[pos].getValue(); }

        // Note: setItem() can't be called on const iterators.
        void setItem(Index pos, const ValueT& v) const { this->parent().mNodes[pos].setValue(v); }
    };// ValueIter

    template<typename NodeT, typename ChildT, typename ValueT, typename TagT>
    struct DenseIter: public DenseIteratorBase<
        MaskDenseIterator, DenseIter<NodeT, ChildT, ValueT, TagT>, NodeT, ChildT, ValueT>
    {
        typedef DenseIteratorBase<MaskDenseIterator, DenseIter, NodeT, ChildT, ValueT> BaseT;
        typedef typename BaseT::NonConstValueType NonConstValueT;

        DenseIter() {}
        DenseIter(const MaskDenseIterator& iter, NodeT* parent):
            DenseIteratorBase<MaskDenseIterator, DenseIter, NodeT, ChildT, ValueT>(iter, parent) {}

        bool getItem(Index pos, ChildT*& child, NonConstValueT& value) const
        {
            child = this->parent().getChildNode(pos);
            if (!child) value = this->parent().mNodes[pos].getValue();
            return (child != NULL);
        }

        // Note: setItem() can't be called on const iterators.
        void setItem(Index pos, ChildT* child) const
        {
            this->parent().setChildNode(pos, child);
        }

        // Note: unsetItem() can't be called on const iterators.
        void unsetItem(Index pos, const ValueT& value) const
        {
            this->parent().unsetChildNode(pos, value);
        }
    };// DenseIter

public:
    // Iterators (see Iterator.h for usage)
    typedef ChildIter<InternalNode, ChildNodeType, MaskOnIterator, ChildOn>          ChildOnIter;
    typedef ChildIter<const InternalNode,const ChildNodeType,MaskOnIterator,ChildOn> ChildOnCIter;
    typedef ValueIter<InternalNode, const ValueType, MaskOffIterator, ChildOff>      ChildOffIter;
    typedef ValueIter<const InternalNode,const ValueType,MaskOffIterator,ChildOff>   ChildOffCIter;
    typedef DenseIter<InternalNode, ChildNodeType, ValueType, ChildAll>              ChildAllIter;
    typedef DenseIter<const InternalNode,const ChildNodeType, ValueType, ChildAll>   ChildAllCIter;

    typedef ValueIter<InternalNode, const ValueType, MaskOnIterator, ValueOn>        ValueOnIter;
    typedef ValueIter<const InternalNode,const ValueType,MaskOnIterator,ValueOn>     ValueOnCIter;
    typedef ValueIter<InternalNode, const ValueType, MaskOffIterator, ValueOff>      ValueOffIter;
    typedef ValueIter<const InternalNode,const ValueType,MaskOffIterator,ValueOff>   ValueOffCIter;
    typedef ValueIter<InternalNode, const ValueType, MaskOffIterator, ValueAll>      ValueAllIter;
    typedef ValueIter<const InternalNode,const ValueType,MaskOffIterator,ValueAll>   ValueAllCIter;

    ChildOnCIter  cbeginChildOn()  const { return ChildOnCIter(mChildMask.beginOn(), this); }
    ChildOffCIter cbeginChildOff() const { return ChildOffCIter(mChildMask.beginOff(), this); }
    ChildAllCIter cbeginChildAll() const { return ChildAllCIter(mChildMask.beginDense(), this); }
    ChildOnCIter   beginChildOn()  const { return cbeginChildOn(); }
    ChildOffCIter  beginChildOff() const { return cbeginChildOff(); }
    ChildAllCIter  beginChildAll() const { return cbeginChildAll(); }
    ChildOnIter    beginChildOn()  { return ChildOnIter(mChildMask.beginOn(), this); }
    ChildOffIter   beginChildOff() { return ChildOffIter(mChildMask.beginOff(), this); }
    ChildAllIter   beginChildAll() { return ChildAllIter(mChildMask.beginDense(), this); }

    ValueOnCIter  cbeginValueOn()  const { return ValueOnCIter(mValueMask.beginOn(), this); }
    ValueOffCIter cbeginValueOff() const { return ValueOffCIter(mValueMask.beginOff(), this); }
    ValueAllCIter cbeginValueAll() const { return ValueAllCIter(mChildMask.beginOff(), this); }
    ValueOnCIter   beginValueOn()  const { return cbeginValueOn(); }
    ValueOffCIter  beginValueOff() const { return cbeginValueOff(); }
    ValueAllCIter  beginValueAll() const { return cbeginValueAll(); }
    ValueOnIter    beginValueOn()  { return ValueOnIter(mValueMask.beginOn(), this); }
    ValueOffIter   beginValueOff() { return ValueOffIter(mValueMask.beginOff(), this); }
    ValueAllIter   beginValueAll() { return ValueAllIter(mChildMask.beginOff(), this); }


    static Index dim() { return DIM; }
    static Index getLevel() { return LEVEL; }
    static void getNodeLog2Dims(std::vector<Index>& dims);
    static Index getChildDim() { return ChildNodeType::DIM; }

    static Index coord2offset(const Coord& xyz);
    static void offset2coord(Index n, Coord& xyz);
    Coord offset2globalCoord(Index n) const;

    Coord getOrigin() const { return mOrigin; }

    Index32 leafCount() const;
    Index32 nonLeafCount() const;
    Index64 onVoxelCount() const;
    Index64 offVoxelCount() const;
    Index64 onLeafVoxelCount() const;
    Index64 offLeafVoxelCount() const;

    /// Return the total amount of memory in bytes occupied by this node and its children.
    Index64 memUsage() const;

    /// @brief Expand the specified bounding box so that it includes the active tiles
    /// of this internal node as well as all the active values in its child nodes.
    void evalActiveVoxelBoundingBox(CoordBBox& bbox) const;

    /// @brief Return the bounding box of this node, i.e., the full index space
    /// spanned by the node regardless of its content.
    CoordBBox getNodeBoundingBox() const { return CoordBBox::createCube(mOrigin, DIM); }

    bool isEmpty() const { return mChildMask.isOff(); }

    /// Return @c true if all of this node's table entries have the same active state
    /// and the same constant value to within the given tolerance,
    /// and return that value in @a constValue and the active state in @a state.
    bool isConstant(ValueType& constValue, bool& state,
        const ValueType& tolerance = zeroVal<ValueType>()) const;
    /// Return @c true if this node has no children and only contains inactive values.
    bool isInactive() const { return this->isChildMaskOff() && this->isValueMaskOff(); }

    /// Return @c true if the voxel at the given coordinates is active.
    bool isValueOn(const Coord& xyz) const;
    /// Return @c true if the voxel at the given offset is active.
    bool isValueOn(Index offset) const { return mValueMask.isOn(offset); }

    /// Return @c true if this node or any of its child nodes have any active tiles.
    bool hasActiveTiles() const;

    const ValueType& getValue(const Coord& xyz) const;
    bool probeValue(const Coord& xyz, ValueType& value) const;

    /// @brief Return the level of the tree (0 = leaf) at which the value
    /// at the given coordinates resides.
    Index getValueLevel(const Coord& xyz) const;

    /// @brief If the first entry in this node's table is a tile, return the tile's value.
    /// Otherwise, return the result of calling getFirstValue() on the child.
    const ValueType& getFirstValue() const;
    /// @brief If the last entry in this node's table is a tile, return the tile's value.
    /// Otherwise, return the result of calling getLastValue() on the child.
    const ValueType& getLastValue() const;

    /// Set the active state at the given coordinates, but don't change its value.
    void setActiveState(const Coord& xyz, bool on);

    /// Mark the voxel at the given coordinates as inactive, but don't change its value.
    void setValueOff(const Coord& xyz);
    /// Change the value of the voxel at the given coordinates and mark the voxel as inactive.
    void setValueOff(const Coord& xyz, const ValueType& value);

    void setValueOn(const Coord& xyz);
    void setValueOn(const Coord& xyz, const ValueType& value);
    void setValueOnly(const Coord& xyz, const ValueType& value);
    void setValueOnMin(const Coord& xyz, const ValueType& value);
    void setValueOnMax(const Coord& xyz, const ValueType& value);
    void setValueOnSum(const Coord& xyz, const ValueType& value);

    /// @brief Set all voxels within an axis-aligned box to a constant value.
    /// (The min and max coordinates are inclusive.)
    void fill(const CoordBBox& bbox, const ValueType&, bool active = true);

    /// @brief Copy into a dense grid the values of the voxels that lie within
    /// a given bounding box.
    ///
    /// @param bbox   inclusive bounding box of the voxels to be copied into the dense grid
    /// @param dense  dense grid with a stride in @e z of one (see tools::Dense
    ///               in tools/Dense.h for the required API)
    ///
    /// @note @a bbox is assumed to be identical to or contained in the coordinate domains
    /// of both the dense grid and this node, i.e., no bounds checking is performed.
    template<typename DenseT>
    void copyToDense(const CoordBBox& bbox, DenseT& dense) const;

    /// Return the value of the voxel at the given coordinates and, if necessary, update
    /// the accessor with pointers to the nodes along the path from the root node to
    /// the node containing the voxel.
    /// @note Used internally by ValueAccessor.
    template<typename AccessorT>
    const ValueType& getValueAndCache(const Coord& xyz, AccessorT&) const;

    /// Return @c true if the voxel at the given coordinates is active and, if necessary,
    /// update the accessor with pointers to the nodes along the path from the root node
    /// to the node containing the voxel.
    /// @note Used internally by ValueAccessor.
    template<typename AccessorT>
    bool isValueOnAndCache(const Coord& xyz, AccessorT&) const;

    /// Change the value of the voxel at the given coordinates and mark it as active.
    /// If necessary, update the accessor with pointers to the nodes along the path
    /// from the root node to the node containing the voxel.
    /// @note Used internally by ValueAccessor.
    template<typename AccessorT>
    void setValueAndCache(const Coord& xyz, const ValueType& value, AccessorT&);

    /// Set the value of the voxel at the given coordinate but preserves its active state.
    /// If necessary, update the accessor with pointers to the nodes along the path
    /// from the root node to the node containing the voxel.
    /// @note Used internally by ValueAccessor.
    template<typename AccessorT>
    void setValueOnlyAndCache(const Coord& xyz, const ValueType& value, AccessorT&);

    /// Set the value of the voxel at the given coordinates to the sum of its current
    /// value and the given value, and mark the voxel as active.
    /// If necessary, update the accessor with pointers to the nodes along the path
    /// from the root node to the node containing the voxel.
    /// @note Used internally by ValueAccessor.
    template<typename AccessorT>
    void setValueOnSumAndCache(const Coord& xyz, const ValueType& value, AccessorT&);

    /// Change the value of the voxel at the given coordinates and mark it as inactive.
    /// If necessary, update the accessor with pointers to the nodes along the path
    /// from the root node to the node containing the voxel.
    /// @note Used internally by ValueAccessor.
    template<typename AccessorT>
    void setValueOffAndCache(const Coord& xyz, const ValueType& value, AccessorT&);

    /// Set the active state of the voxel at the given coordinates without changing its value.
    /// If necessary, update the accessor with pointers to the nodes along the path
    /// from the root node to the node containing the voxel.
    /// @note Used internally by ValueAccessor.
    template<typename AccessorT>
    void setActiveStateAndCache(const Coord& xyz, bool on, AccessorT&);

    /// Return @c true if the voxel at the given coordinates is active, change the voxel's
    /// value, and, if necessary, update the accessor with pointers to the nodes along
    /// the path from the root node to the node containing the voxel.
    /// @note Used internally by ValueAccessor.
    template<typename AccessorT>
    bool probeValueAndCache(const Coord& xyz, ValueType& value, AccessorT&) const;

    /// @brief Return the level of the tree (0 = leaf) at which the value
    /// at the given coordinates resides.
    ///
    /// If necessary, update the accessor with pointers to the nodes along the path
    /// from the root node to the node containing the voxel.
    /// @note Used internally by ValueAccessor.
    template<typename AccessorT>
    Index getValueLevelAndCache(const Coord& xyz, AccessorT&) const;

    /// Mark all values (both tiles and voxels) as active.
    void setValuesOn();

    //
    // I/O
    //
    void writeTopology(std::ostream&, bool toHalf = false) const;
    void readTopology(std::istream&, bool fromHalf = false);
    void writeBuffers(std::ostream&, bool toHalf = false) const;
    void readBuffers(std::istream&, bool fromHalf = false);

    /// @brief Overwrites the inactive values with a new value whos
    /// magnitude is equal to the specified background value and sign
    /// is consistant with the lexicographically closest active value.
    /// The net effect is a propagation of signs from the active to the
    /// inactive values. Note this flood-filling is also performed on
    /// any child nodes.
    ///
    /// @note This method is primarily useful for propagating the sign
    /// from the (active) voxels in a narrow-band level set to the
    /// inactive values outside the narrow band.
    void signedFloodFill(const ValueType& background);

    /// @brief Sets the inactive values to either the outside or inside
    /// value, depending on the sign of the closest corresponding
    /// active value. More specefically, an inactive value is set to
    /// the outside value if the closest active value in the
    /// lexicographic direction is positive, else it is set to the
    /// inside value. Note this operation is also performed on any child nodes.
    void signedFloodFill(const ValueType& outside, const ValueType& inside);

    /// Change the sign of all the values represented in this node and
    /// its child nodes.
    void negate();

    /// Replace active tiles with dense voxels, i.e., with active leaf nodes.
    void voxelizeActiveTiles();

    /// @brief Simple merge: Nodes and values of this node are always unchanged!
    ///
    /// @note Nodes and values of the other node are simply merged into this
    /// node and the other tree is cannibalized in the process!
    void merge(InternalNode& other, const ValueType& background, const ValueType& otherBackground);

    /// @brief Union this branch's set of active values with the other branch's
    /// active values.  The value type of the other branch can be different.
    /// @details The resulting state of a value is active if the corresponding value
    /// was already active OR if it is active in the other tree.  Also, a resulting
    /// value maps to a voxel if the corresponding value already mapped to a voxel
    /// OR if it is a voxel in the other tree.  Thus, a resulting value can only
    /// map to a tile if the corresponding value already mapped to a tile
    /// AND if it is a tile value in other tree.
    ///
    /// Specifically, active tiles and voxels in this branch are not changed, and
    /// tiles or voxels that were inactive in this branch but active in the other branch
    /// are marked as active in this branch but left with their original values.
    template<typename OtherChildNodeType>
    void topologyUnion(const InternalNode<OtherChildNodeType, Log2Dim>& other);

    template<typename CombineOp>
    void combine(InternalNode& other, CombineOp&);
    template<typename CombineOp>
    void combine(const ValueType& value, bool valueIsActive, CombineOp&);

    template<typename CombineOp>
    void combine2(const InternalNode& other0, const InternalNode& other1, CombineOp&);
    template<typename CombineOp>
    void combine2(const ValueType& value, const InternalNode& other,
                  bool valueIsActive, CombineOp&);
    template<typename CombineOp>
    void combine2(const InternalNode& other, const ValueType& value,
                  bool valueIsActive, CombineOp&);

    /// @brief Calls the templated functor BBoxOp with bounding box
    /// information for all active tiles and leaf nodes in this node.
    /// An additional level argument is provided for each callback.
    ///
    /// @note The bounding boxes are guarenteed to be non-overlapping.
    template<typename BBoxOp> void visitActiveBBox(BBoxOp&) const;

    template<typename VisitorOp> void visit(VisitorOp&);
    template<typename VisitorOp> void visit(VisitorOp&) const;

    template<typename OtherNodeType, typename VisitorOp>
    void visit2Node(OtherNodeType& other, VisitorOp&);
    template<typename OtherNodeType, typename VisitorOp>
    void visit2Node(OtherNodeType& other, VisitorOp&) const;
    template<typename IterT, typename VisitorOp>
    void visit2(IterT& otherIter, VisitorOp&, bool otherIsLHS = false);
    template<typename IterT, typename VisitorOp>
    void visit2(IterT& otherIter, VisitorOp&, bool otherIsLHS = false) const;

    /// @brief Call the @c PruneOp functor for each child node and, if the functor
    /// returns @c true, prune the node and replace it with a tile.
    ///
    /// This method is used to implement all of the various pruning algorithms
    /// (prune(), pruneInactive(), etc.).  It should rarely be called directly.
    /// @see openvdb/tree/Util.h for the definition of the @c PruneOp functor
    template<typename PruneOp> void pruneOp(PruneOp&);

    /// @brief Reduce the memory footprint of this tree by replacing with tiles
    /// any nodes whose values are all the same (optionally to within a tolerance)
    /// and have the same active state.
    void prune(const ValueType& tolerance = zeroVal<ValueType>());

    /// @brief Reduce the memory footprint of this tree by replacing with
    /// tiles of the given value any nodes whose values are all inactive.
    void pruneInactive(const ValueType&);

    /// @brief Reduce the memory footprint of this tree by replacing with
    /// background tiles any nodes whose values are all inactive.
    void pruneInactive();

    /// @brief Add the specified leaf to this node, possibly creating a child branch
    /// in the process.  If the leaf node already exists, replace it.
    void addLeaf(LeafNodeType* leaf);

    /// @brief Same as addLeaf except, if necessary, it update the accessor with pointers
    /// to the nodes along the path from the root node to the node containing the coordinate.
    template<typename AccessorT>
    void addLeafAndCache(LeafNodeType* leaf, AccessorT&);

    /// @brief Return a pointer to the node of type @c NodeT that contains voxel (x, y, z)
    /// and replace it with a tile of the specified value and state.
    /// If no such node exists, leave the tree unchanged and return @c NULL.
    ///
    /// @note The caller takes ownership of the node and is responsible for deleting it.
    ///
    /// @warning Since this method potentially removes nodes and branches of the tree,
    /// it is important to clear the caches of all ValueAccessors associated with this tree.
    template<typename NodeT>
    NodeT* stealNode(const Coord& xyz, const ValueType& value, bool state);

    /// @brief Add a tile at the specified tree level that contains voxel (x, y, z),
    /// possibly creating a parent branch or deleting a child branch in the process.
    void addTile(Index level, const Coord& xyz, const ValueType& value, bool state);

    /// @brief Same as addTile() except, if necessary, update the accessor with pointers
    /// to the nodes along the path from the root node to the node containing (x, y, z).
    template<typename AccessorT>
    void addTileAndCache(Index level, const Coord& xyz, const ValueType& value,
                         bool state, AccessorT&);

    /// @brief Return a pointer to the node that contains voxel (x, y, z).
    /// If no such node exists, return NULL.
    template <typename NodeType>
    NodeType* probeNode(const Coord& xyz);
    template <typename NodeType>
    const NodeType* probeConstNode(const Coord& xyz) const;
    template<typename NodeType, typename AccessorT>
    NodeType* probeNodeAndCache(const Coord& xyz, AccessorT&);
    template<typename NodeType, typename AccessorT>
    const NodeType* probeConstNodeAndCache(const Coord& xyz, AccessorT&) const;
    
    /// @brief Return a pointer to the leaf node that contains voxel (x, y, z).
    /// If no such node exists, return NULL.
    LeafNodeType* probeLeaf(const Coord& xyz)
    {
        return this->template probeNode<LeafNodeType>(xyz);
    }

    /// @brief Return a const pointer to the leaf node that contains voxel (x, y, z).
    /// If no such node exists, return NULL.
    const LeafNodeType* probeConstLeaf(const Coord& xyz) const
    {
        return this->template probeConstNode<LeafNodeType>(xyz);
    }
    const LeafNodeType* probeLeaf(const Coord& xyz) const { return this->probeConstLeaf(xyz); }

    /// @brief Same as probeLeaf except, if necessary, it update the accessor with pointers
    /// to the nodes along the path from the root node to the node containing the coordinate.
    template<typename AccessorT>
    LeafNodeType* probeLeafAndCache(const Coord& xyz, AccessorT& acc)
    {
        return this->template probeNodeAndCache<LeafNodeType>(xyz, acc);
    }

    /// @brief Same as probeLeaf except, if necessary, it update the accessor with pointers
    /// to the nodes along the path from the root node to the node containing the coordinate.
    template<typename AccessorT>
    const LeafNodeType* probeConstLeafAndCache(const Coord& xyz, AccessorT& acc) const
    {
        return this->template probeConstNodeAndCache<LeafNodeType>(xyz, acc);
    }
    /// @brief Same as probeConstLeafAndCache
    template<typename AccessorT>
    const LeafNodeType* probeLeafAndCache(const Coord& xyz, AccessorT& acc) const
    {
        return this->probeConstLeafAndCache(xyz, acc);
    }

    /// @brief Return the leaf node that contains voxel (x, y, z).
    /// If no such node exists, create one, but preserve the values and
    /// active states of all voxels.
    ///
    /// @details Use this method to preallocate a static tree topology
    /// over which to safely perform multithreaded processing.
    LeafNodeType* touchLeaf(const Coord& xyz);

    /// @brief Same as touchLeaf except, if necessary, it update the accessor with pointers
    /// to the nodes along the path from the root node to the node containing the coordinate.
    template<typename AccessorT>
    LeafNodeType* touchLeafAndCache(const Coord& xyz, AccessorT&);

    /// @brief Change inactive tiles or voxels with value oldBackground to newBackground
    /// or -oldBackground to -newBackground. Active values are unchanged.
    void resetBackground(const ValueType& oldBackground, const ValueType& newBackground);

    /// @brief Return @c true if the given tree branch has the same node and active value
    /// topology as this tree branch (but possibly a different @c ValueType).
    template<typename OtherChildNodeType, Index OtherLog2Dim>
    bool hasSameTopology(const InternalNode<OtherChildNodeType, OtherLog2Dim>* other) const;

protected:
    //@{
    /// Allow iterators to call mask accessor methods (setValueMask(), setChildMask(), etc.).
    /// @todo Make mask accessors public?
    friend class IteratorBase<MaskOnIterator, InternalNode>;
    friend class IteratorBase<MaskOffIterator, InternalNode>;
    friend class IteratorBase<MaskDenseIterator, InternalNode>;
    //@}

    /// @brief During topology-only construction, access is needed
    /// to protected/private members of other template instances.
    template<typename, Index> friend class InternalNode;

    // Mask accessors
public:
    bool isValueMaskOn(Index n) const { return mValueMask.isOn(n); }
    bool isValueMaskOn() const { return mValueMask.isOn(); }
    bool isValueMaskOff(Index n) const { return mValueMask.isOff(n); }
    bool isValueMaskOff() const { return mValueMask.isOff(); }
    bool isChildMaskOn(Index n) const { return mChildMask.isOn(n); }
    bool isChildMaskOff(Index n) const { return mChildMask.isOff(n); }
    bool isChildMaskOff() const { return mChildMask.isOff(); }
protected:
    //@{
    /// Use a mask accessor to ensure consistency between the child and value masks;
    /// i.e., the value mask should always be off wherever the child mask is on.
    void setValueMask(Index n, bool on) { mValueMask.set(n, mChildMask.isOn(n) ? false : on); }
    //@}

    void makeChildNodeEmpty(Index n, const ValueType& value);
    void setChildNode(Index i, ChildNodeType* child);
    ChildNodeType* unsetChildNode(Index i, const ValueType& value);

    template<typename NodeT, typename VisitorOp, typename ChildAllIterT>
    static inline void doVisit(NodeT&, VisitorOp&);

    template<typename NodeT, typename OtherNodeT, typename VisitorOp,
        typename ChildAllIterT, typename OtherChildAllIterT>
    static inline void doVisit2Node(NodeT&, OtherNodeT&, VisitorOp&);

    template<typename NodeT, typename VisitorOp,
        typename ChildAllIterT, typename OtherChildAllIterT>
    static inline void doVisit2(NodeT&, OtherChildAllIterT&, VisitorOp&, bool otherIsLHS);

    ChildNodeType* getChildNode(Index n);
    const ChildNodeType* getChildNode(Index n) const;


    UnionType mNodes[NUM_VALUES];
    NodeMaskType mChildMask, mValueMask;
    /// Global grid index coordinates (x,y,z) of the local origin of this node
    Coord mOrigin;
}; // class InternalNode


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
inline
InternalNode<ChildT, Log2Dim>::InternalNode(const ValueType& background)
{
    for (Index i = 0; i < NUM_VALUES; ++i) mNodes[i].setValue(background);
}


template<typename ChildT, Index Log2Dim>
inline
InternalNode<ChildT, Log2Dim>::InternalNode(const Coord& origin, const ValueType& val, bool active):
    mOrigin(origin[0] & ~(DIM - 1), // zero out the low-order bits
            origin[1] & ~(DIM - 1),
            origin[2] & ~(DIM - 1))
{
    if (active) mValueMask.setOn();
    for (Index i = 0; i < NUM_VALUES; ++i) mNodes[i].setValue(val);
}


template<typename ChildT, Index Log2Dim>
inline
InternalNode<ChildT, Log2Dim>::InternalNode(const InternalNode& other):
    mChildMask(other.mChildMask),
    mValueMask(other.mValueMask),
    mOrigin(other.mOrigin)
{
    for (Index i = 0; i < NUM_VALUES; ++i) {
        if (isChildMaskOn(i)) {
            mNodes[i].setChild(new ChildNodeType(*(other.mNodes[i].getChild())));
        } else {
            mNodes[i].setValue(other.mNodes[i].getValue());
        }
    }
}

template<typename ChildT, Index Log2Dim>
template<typename OtherChildNodeType>
inline
InternalNode<ChildT, Log2Dim>::InternalNode(const InternalNode<OtherChildNodeType, Log2Dim>& other,
    const ValueType& offValue, const ValueType& onValue, TopologyCopy):
    mChildMask(other.mChildMask),
    mValueMask(other.mValueMask),
    mOrigin(other.mOrigin)
{
    for (Index i = 0; i < NUM_VALUES; ++i) {
        if (isChildMaskOn(i)) {
            mNodes[i].setChild(new ChildNodeType(*(other.mNodes[i].getChild()),
                                                 offValue, onValue, TopologyCopy()));
        } else {
            mNodes[i].setValue(isValueMaskOn(i) ? onValue : offValue);
        }
    }
}

template<typename ChildT, Index Log2Dim>
template<typename OtherChildNodeType>
inline
InternalNode<ChildT, Log2Dim>::InternalNode(const InternalNode<OtherChildNodeType, Log2Dim>& other,
                                            const ValueType& background, TopologyCopy):
    mChildMask(other.mChildMask),
    mValueMask(other.mValueMask),
    mOrigin(other.mOrigin)
{
    for (Index i = 0; i < NUM_VALUES; ++i) mNodes[i].setValue(background);
    for (ChildOnIter iter = this->beginChildOn(); iter; ++iter) {
        mNodes[iter.pos()].setChild(new ChildNodeType(*(other.mNodes[iter.pos()].getChild()),
                                                      background, TopologyCopy()));
    }
}


template<typename ChildT, Index Log2Dim>
inline
InternalNode<ChildT, Log2Dim>::~InternalNode()
{
    for (ChildOnIter iter = this->beginChildOn(); iter; ++iter) {
        delete mNodes[iter.pos()].getChild();
    }
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
inline Index32
InternalNode<ChildT, Log2Dim>::leafCount() const
{
    if (ChildNodeType::getLevel() == 0) return mChildMask.countOn();
    Index32 sum = 0;
    for (ChildOnCIter iter = this->cbeginChildOn(); iter; ++iter) {
        sum += iter->leafCount();
    }
    return sum;
}


template<typename ChildT, Index Log2Dim>
inline Index32
InternalNode<ChildT, Log2Dim>::nonLeafCount() const
{
    Index32 sum = 1;
    if (ChildNodeType::getLevel() == 0) return sum;
    for (ChildOnCIter iter = this->cbeginChildOn(); iter; ++iter) {
        sum += iter->nonLeafCount();
    }
    return sum;
}


template<typename ChildT, Index Log2Dim>
inline Index64
InternalNode<ChildT, Log2Dim>::onVoxelCount() const
{
    Index64 sum = 0;
    for (Index i = 0; i < NUM_VALUES; ++i) {
        if (isChildMaskOff(i)) {
            if (isValueMaskOn(i)) sum += ChildT::NUM_VOXELS;
        } else {
            sum += mNodes[i].getChild()->onVoxelCount();
        }
    }
    return sum;
}


template<typename ChildT, Index Log2Dim>
inline Index64
InternalNode<ChildT, Log2Dim>::offVoxelCount() const
{
    Index64 sum = 0;
    for (Index i = 0; i < NUM_VALUES; ++i) {
        if (isChildMaskOff(i)) {
            if (isValueMaskOff(i)) sum += ChildT::NUM_VOXELS;
        } else {
            sum += mNodes[i].getChild()->offVoxelCount();
        }
    }
    return sum;
}


template<typename ChildT, Index Log2Dim>
inline Index64
InternalNode<ChildT, Log2Dim>::onLeafVoxelCount() const
{
    Index64 sum = 0;
    for (ChildOnCIter iter = this->beginChildOn(); iter; ++iter) {
        sum += mNodes[iter.pos()].getChild()->onLeafVoxelCount();
    }
    return sum;
}


template<typename ChildT, Index Log2Dim>
inline Index64
InternalNode<ChildT, Log2Dim>::offLeafVoxelCount() const
{
    Index64 sum = 0;
    for (ChildOnCIter iter = this->beginChildOn(); iter; ++iter) {
        sum += mNodes[iter.pos()].getChild()->offLeafVoxelCount();
    }
    return sum;
}


template<typename ChildT, Index Log2Dim>
inline Index64
InternalNode<ChildT, Log2Dim>::memUsage() const
{
    Index64 sum = NUM_VALUES * sizeof(UnionType) + mChildMask.memUsage()
                + mValueMask.memUsage() + sizeof(mOrigin);
    for (ChildOnCIter iter = this->cbeginChildOn(); iter; ++iter) {
        sum += iter->memUsage();
    }
    return sum;
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::evalActiveVoxelBoundingBox(CoordBBox& bbox) const
{
    if (bbox.isInside(this->getNodeBoundingBox())) return;

    ValueType dummy;
    for (ChildAllCIter iter = this->cbeginChildAll(); iter; ++iter) {
        if (const ChildT* child = iter.probeChild(dummy)) {
            child->evalActiveVoxelBoundingBox(bbox);
        } else if (iter.isValueOn()) {
            bbox.expand(iter.getCoord(), ChildT::DIM);
        }
    }
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
template<typename PruneOp>
inline void
InternalNode<ChildT, Log2Dim>::pruneOp(PruneOp& op)
{
    for (ChildOnIter iter = this->beginChildOn(); iter; ++iter) {
        const Index i = iter.pos();
        ChildT* child = mNodes[i].getChild();
        if (!op(*child)) continue;
        delete child;
        mChildMask.setOff(i);
        mValueMask.set(i, op.state);
        mNodes[i].setValue(op.value);
    }

}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::prune(const ValueType& tolerance)
{
    TolerancePrune<ValueType> op(tolerance);
    this->pruneOp(op);
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::pruneInactive(const ValueType& bg)
{
    InactivePrune<ValueType> op(bg);
    this->pruneOp(op);
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::pruneInactive()
{
    this->pruneInactive(this->getBackground());
}


////////////////////////////////////////

template<typename ChildT, Index Log2Dim>
template<typename NodeT>
inline NodeT*
InternalNode<ChildT, Log2Dim>::stealNode(const Coord& xyz, const ValueType& value, bool state)
{
    if ((NodeT::LEVEL == ChildT::LEVEL && !(boost::is_same<NodeT, ChildT>::value)) ||
         NodeT::LEVEL >  ChildT::LEVEL) return NULL;
    OPENVDB_NO_UNREACHABLE_CODE_WARNING_BEGIN
    const Index n = this->coord2offset(xyz);
    if (mChildMask.isOff(n)) return NULL;
    ChildT* child = mNodes[n].getChild();
    if (boost::is_same<NodeT, ChildT>::value) {
        mChildMask.setOff(n);
        mValueMask.set(n, state);
        mNodes[n].setValue(value);
    }
    return (boost::is_same<NodeT, ChildT>::value)
        ? reinterpret_cast<NodeT*>(child)
        : child->template stealNode<NodeT>(xyz, value, state);
    OPENVDB_NO_UNREACHABLE_CODE_WARNING_END
}

////////////////////////////////////////

template<typename ChildT, Index Log2Dim>
template<typename NodeT>
inline NodeT*
InternalNode<ChildT, Log2Dim>::probeNode(const Coord& xyz)
{
    if ((NodeT::LEVEL == ChildT::LEVEL && !(boost::is_same<NodeT, ChildT>::value)) ||
         NodeT::LEVEL >  ChildT::LEVEL) return NULL;
    OPENVDB_NO_UNREACHABLE_CODE_WARNING_BEGIN
    const Index n = this->coord2offset(xyz);
    if (mChildMask.isOff(n)) return NULL;
    ChildT* child = mNodes[n].getChild();
    return (boost::is_same<NodeT, ChildT>::value)
           ? reinterpret_cast<NodeT*>(child)
           : child->template probeNode<NodeT>(xyz);
    OPENVDB_NO_UNREACHABLE_CODE_WARNING_END
}

template<typename ChildT, Index Log2Dim>
template<typename NodeT, typename AccessorT>
inline NodeT*
InternalNode<ChildT, Log2Dim>::probeNodeAndCache(const Coord& xyz, AccessorT& acc)
{
    if ((NodeT::LEVEL == ChildT::LEVEL && !(boost::is_same<NodeT, ChildT>::value)) ||
         NodeT::LEVEL >  ChildT::LEVEL) return NULL;
    OPENVDB_NO_UNREACHABLE_CODE_WARNING_BEGIN
    const Index n = this->coord2offset(xyz);
    if (mChildMask.isOff(n)) return NULL;
    ChildT* child = mNodes[n].getChild();
    acc.insert(xyz, child);
    return (boost::is_same<NodeT, ChildT>::value)
           ? reinterpret_cast<NodeT*>(child)
           : child->template probeNodeAndCache<NodeT>(xyz, acc);
    OPENVDB_NO_UNREACHABLE_CODE_WARNING_END
}

template<typename ChildT, Index Log2Dim>
template<typename NodeT>
inline const NodeT*
InternalNode<ChildT, Log2Dim>::probeConstNode(const Coord& xyz) const
{
    if ((NodeT::LEVEL == ChildT::LEVEL && !(boost::is_same<NodeT, ChildT>::value)) ||
         NodeT::LEVEL >  ChildT::LEVEL) return NULL;
    OPENVDB_NO_UNREACHABLE_CODE_WARNING_BEGIN
    const Index n = this->coord2offset(xyz);
    if (mChildMask.isOff(n)) return NULL;
    const ChildT* child = mNodes[n].getChild();
    return (boost::is_same<NodeT, ChildT>::value)
            ? reinterpret_cast<const NodeT*>(child)
            : child->template probeConstNode<NodeT>(xyz);
    OPENVDB_NO_UNREACHABLE_CODE_WARNING_END
}
 
template<typename ChildT, Index Log2Dim>
template<typename NodeT, typename AccessorT>
inline const NodeT*
InternalNode<ChildT, Log2Dim>::probeConstNodeAndCache(const Coord& xyz, AccessorT& acc) const
{
    if ((NodeT::LEVEL == ChildT::LEVEL && !(boost::is_same<NodeT, ChildT>::value)) ||
         NodeT::LEVEL >  ChildT::LEVEL) return NULL;
    OPENVDB_NO_UNREACHABLE_CODE_WARNING_BEGIN
    const Index n = this->coord2offset(xyz);
    if (mChildMask.isOff(n)) return NULL;
    const ChildT* child = mNodes[n].getChild();
    acc.insert(xyz, child);
    return (boost::is_same<NodeT, ChildT>::value)
            ? reinterpret_cast<const NodeT*>(child)
            : child->template probeConstNodeAndCache<NodeT>(xyz, acc);
    OPENVDB_NO_UNREACHABLE_CODE_WARNING_END
}

////////////////////////////////////////

template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::addLeaf(LeafNodeType* leaf)
{
    assert(leaf != NULL);
    const Coord& xyz = leaf->origin();
    const Index n = this->coord2offset(xyz);
    ChildT* child = NULL;
    if (mChildMask.isOff(n)) {
        if (ChildT::LEVEL>0) {
            child = new ChildT(xyz, mNodes[n].getValue(), mValueMask.isOn(n));
        } else {
            child = reinterpret_cast<ChildT*>(leaf);
        }
        mNodes[n].setChild(child);
        mChildMask.setOn(n);
        mValueMask.setOff(n);
    } else {
        if (ChildT::LEVEL>0) {
            child = mNodes[n].getChild();
        } else {
            delete mNodes[n].getChild();
            child = reinterpret_cast<ChildT*>(leaf);
            mNodes[n].setChild(child);
        }
    }
    child->addLeaf(leaf);
}


template<typename ChildT, Index Log2Dim>
template<typename AccessorT>
inline void
InternalNode<ChildT, Log2Dim>::addLeafAndCache(LeafNodeType* leaf, AccessorT& acc)
{
    assert(leaf != NULL);
    const Coord& xyz = leaf->origin();
    const Index n = this->coord2offset(xyz);
    ChildT* child = NULL;
    if (mChildMask.isOff(n)) {
        if (ChildT::LEVEL>0) {
            child = new ChildT(xyz, mNodes[n].getValue(), mValueMask.isOn(n));
            acc.insert(xyz, child);//we only cache internal nodes
        } else {
            child = reinterpret_cast<ChildT*>(leaf);
        }
        mNodes[n].setChild(child);
        mChildMask.setOn(n);
        mValueMask.setOff(n);
    } else {
        if (ChildT::LEVEL>0) {
            child = mNodes[n].getChild();
            acc.insert(xyz, child);//we only cache internal nodes
        } else {
            delete mNodes[n].getChild();
            child = reinterpret_cast<ChildT*>(leaf);
            mNodes[n].setChild(child);
        }
    }
    child->addLeafAndCache(leaf, acc);
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::addTile(Index level, const Coord& xyz,
                                       const ValueType& value, bool state)
{
    assert(level > 0);
    if (LEVEL >= level) {
        const Index n = this->coord2offset(xyz);
        if (mChildMask.isOff(n)) {// tile case
            if (LEVEL > level) {
                ChildT* child = new ChildT(xyz, mNodes[n].getValue(), mValueMask.isOn(n));
                mNodes[n].setChild(child);
                mChildMask.setOn(n);
                mValueMask.setOff(n);
                child->addTile(level, xyz, value, state);
            } else {
                mValueMask.set(n, state);
                mNodes[n].setValue(value);
            }
        } else {// child branch case
            ChildT* child = mNodes[n].getChild();
            if (LEVEL > level) {
                child->addTile(level, xyz, value, state);
            } else {
                delete child;
                mChildMask.setOff(n);
                mValueMask.set(n, state);
                mNodes[n].setValue(value);
            }
        }
    }
}


template<typename ChildT, Index Log2Dim>
template<typename AccessorT>
inline void
InternalNode<ChildT, Log2Dim>::addTileAndCache(Index level, const Coord& xyz,
    const ValueType& value, bool state, AccessorT& acc)
{
    assert(level > 0);
    if (LEVEL >= level) {
        const Index n = this->coord2offset(xyz);
        if (mChildMask.isOff(n)) {// tile case
            if (LEVEL > level) {
                ChildT* child = new ChildT(xyz, mNodes[n].getValue(), mValueMask.isOn(n));
                mNodes[n].setChild(child);
                mChildMask.setOn(n);
                mValueMask.setOff(n);
                acc.insert(xyz, child);
                child->addTileAndCache(level, xyz, value, state, acc);
            } else {
                mValueMask.set(n, state);
                mNodes[n].setValue(value);
            }
        } else {// child branch case
            ChildT* child = mNodes[n].getChild();
            if (LEVEL > level) {
                acc.insert(xyz, child);
                child->addTileAndCache(level, xyz, value, state, acc);
            } else {
                delete child;
                mChildMask.setOff(n);
                mValueMask.set(n, state);
                mNodes[n].setValue(value);
            }
        }
    }
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
inline typename ChildT::LeafNodeType*
InternalNode<ChildT, Log2Dim>::touchLeaf(const Coord& xyz)
{
    const Index n = this->coord2offset(xyz);
    ChildT* child = NULL;
    if (mChildMask.isOff(n)) {
        child = new ChildT(xyz, mNodes[n].getValue(), mValueMask.isOn(n));
        mNodes[n].setChild(child);
        mChildMask.setOn(n);
        mValueMask.setOff(n);
    } else {
        child = mNodes[n].getChild();
    }
    return child->touchLeaf(xyz);
}


template<typename ChildT, Index Log2Dim>
template<typename AccessorT>
inline typename ChildT::LeafNodeType*
InternalNode<ChildT, Log2Dim>::touchLeafAndCache(const Coord& xyz, AccessorT& acc)
{
    const Index n = this->coord2offset(xyz);
    if (mChildMask.isOff(n)) {
        mNodes[n].setChild(new ChildNodeType(xyz, mNodes[n].getValue(), mValueMask.isOn(n)));
        mChildMask.setOn(n);
        mValueMask.setOff(n);
    }
    acc.insert(xyz, mNodes[n].getChild());
    return mNodes[n].getChild()->touchLeafAndCache(xyz, acc);
}
 

////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
inline bool
InternalNode<ChildT, Log2Dim>::isConstant(ValueType& constValue, bool& state,
    const ValueType& tolerance) const
{
    bool allEqual = true, firstValue = true, valueState = true;
    ValueType value = zeroVal<ValueType>();
    for (Index i = 0; allEqual && i < NUM_VALUES; ++i) {
        if (this->isChildMaskOff(i)) {
            // If entry i is a value, check if it is within tolerance
            // and whether its active state matches the other entries.
            if (firstValue) {
                firstValue = false;
                valueState = isValueMaskOn(i);
                value = mNodes[i].getValue();
            } else {
                allEqual = (isValueMaskOn(i) == valueState)
                    && math::isApproxEqual(mNodes[i].getValue(), value, tolerance);
            }
        } else {
            // If entry i is a child, check if the child is constant and within tolerance
            // and whether its active state matches the other entries.
            ValueType childValue = zeroVal<ValueType>();
            bool isChildOn = false;
            if (mNodes[i].getChild()->isConstant(childValue, isChildOn, tolerance)) {
                if (firstValue) {
                    firstValue = false;
                    valueState = isChildOn;
                    value = childValue;
                } else {
                    allEqual = (isChildOn == valueState)
                        && math::isApproxEqual(childValue, value, tolerance);
                }
            } else { // child is not constant
                allEqual = false;
            }
        }
    }
    if (allEqual) {
        constValue = value;
        state = valueState;
    }
    return allEqual;
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
inline bool
InternalNode<ChildT, Log2Dim>::hasActiveTiles() const
{
    const bool anyActiveTiles = !mValueMask.isOff();
    if (LEVEL==1 || anyActiveTiles) return anyActiveTiles;
    for (ChildOnCIter iter = this->cbeginChildOn(); iter; ++iter) {
        if (iter->hasActiveTiles()) return true;
    }
    return false;
}


template<typename ChildT, Index Log2Dim>
inline bool
InternalNode<ChildT, Log2Dim>::isValueOn(const Coord& xyz) const
{
    const Index n = this->coord2offset(xyz);
    if (this->isChildMaskOff(n)) return this->isValueMaskOn(n);
    return mNodes[n].getChild()->isValueOn(xyz);
}

template<typename ChildT, Index Log2Dim>
template<typename AccessorT>
inline bool
InternalNode<ChildT, Log2Dim>::isValueOnAndCache(const Coord& xyz, AccessorT& acc) const
{
    const Index n = this->coord2offset(xyz);
    if (this->isChildMaskOff(n)) return this->isValueMaskOn(n);
    acc.insert(xyz, mNodes[n].getChild());
    return mNodes[n].getChild()->isValueOnAndCache(xyz, acc);
}


template<typename ChildT, Index Log2Dim>
inline const typename ChildT::ValueType&
InternalNode<ChildT, Log2Dim>::getValue(const Coord& xyz) const
{
    const Index n = this->coord2offset(xyz);
    return this->isChildMaskOff(n) ? mNodes[n].getValue()
        :  mNodes[n].getChild()->getValue(xyz);
}

template<typename ChildT, Index Log2Dim>
template<typename AccessorT>
inline const typename ChildT::ValueType&
InternalNode<ChildT, Log2Dim>::getValueAndCache(const Coord& xyz, AccessorT& acc) const
{
    const Index n = this->coord2offset(xyz);
    if (this->isChildMaskOn(n)) {
        acc.insert(xyz, mNodes[n].getChild());
        return mNodes[n].getChild()->getValueAndCache(xyz, acc);
    }
    return mNodes[n].getValue();
}


template<typename ChildT, Index Log2Dim>
inline Index
InternalNode<ChildT, Log2Dim>::getValueLevel(const Coord& xyz) const
{
    const Index n = this->coord2offset(xyz);
    return this->isChildMaskOff(n) ? LEVEL : mNodes[n].getChild()->getValueLevel(xyz);
}

template<typename ChildT, Index Log2Dim>
template<typename AccessorT>
inline Index
InternalNode<ChildT, Log2Dim>::getValueLevelAndCache(const Coord& xyz, AccessorT& acc) const
{
    const Index n = this->coord2offset(xyz);
    if (this->isChildMaskOn(n)) {
        acc.insert(xyz, mNodes[n].getChild());
        return mNodes[n].getChild()->getValueLevelAndCache(xyz, acc);
    }
    return LEVEL;
}


template<typename ChildT, Index Log2Dim>
inline bool
InternalNode<ChildT, Log2Dim>::probeValue(const Coord& xyz, ValueType& value) const
{
    const Index n = this->coord2offset(xyz);
    if (this->isChildMaskOff(n)) {
        value = mNodes[n].getValue();
        return this->isValueMaskOn(n);
    }
    return mNodes[n].getChild()->probeValue(xyz, value);
}

template<typename ChildT, Index Log2Dim>
template<typename AccessorT>
inline bool
InternalNode<ChildT, Log2Dim>::probeValueAndCache(const Coord& xyz,
    ValueType& value, AccessorT& acc) const
{
    const Index n = this->coord2offset(xyz);
    if (this->isChildMaskOn(n)) {
        acc.insert(xyz, mNodes[n].getChild());
        return mNodes[n].getChild()->probeValueAndCache(xyz, value, acc);
    }
    value = mNodes[n].getValue();
    return this->isValueMaskOn(n);
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::setValueOff(const Coord& xyz)
{
    const Index n = this->coord2offset(xyz);
    bool hasChild = this->isChildMaskOn(n);
    if (!hasChild && this->isValueMaskOn(n)) {
        // If the voxel belongs to a constant tile that is active,
        // a child subtree must be constructed.
        mChildMask.setOn(n); // we're adding a child node so set the mask on
        mValueMask.setOff(n); // value mask is always off if child mask is on
        hasChild = true;
        mNodes[n].setChild(new ChildNodeType(xyz, mNodes[n].getValue(), /*active=*/true));
    }
    if (hasChild) mNodes[n].getChild()->setValueOff(xyz);
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::setValueOn(const Coord& xyz)
{
    const Index n = this->coord2offset(xyz);
    bool hasChild = this->isChildMaskOn(n);
    if (!hasChild && !this->isValueMaskOn(n)) {
        // If the voxel belongs to a constant tile that is inactive,
        // a child subtree must be constructed.
        mChildMask.setOn(n); // we're adding a child node so set the mask on
        mValueMask.setOff(n); // value mask is always off if child mask is on
        hasChild = true;
        mNodes[n].setChild(new ChildNodeType(xyz, mNodes[n].getValue(), /*active=*/false));
    }
    if (hasChild) mNodes[n].getChild()->setValueOn(xyz);
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::setValueOff(const Coord& xyz, const ValueType& value)
{
    const Index n = InternalNode::coord2offset(xyz);
    bool hasChild = this->isChildMaskOn(n);
    if (!hasChild) {
        const bool active = this->isValueMaskOn(n);
        if (active || !math::isExactlyEqual(mNodes[n].getValue(), value)) {
            // If the voxel belongs to a tile that is either active or that
            // has a constant value that is different from the one provided,
            // a child subtree must be constructed.
            mChildMask.setOn(n); // we're adding a child node so set the mask on
            mValueMask.setOff(n); // value mask is always off if child mask is on
            hasChild = true;
            mNodes[n].setChild(new ChildNodeType(xyz, mNodes[n].getValue(), active));
        }
    }
    if (hasChild) mNodes[n].getChild()->setValueOff(xyz, value);
}

template<typename ChildT, Index Log2Dim>
template<typename AccessorT>
inline void
InternalNode<ChildT, Log2Dim>::setValueOffAndCache(const Coord& xyz,
    const ValueType& value, AccessorT& acc)
{
    const Index n = InternalNode::coord2offset(xyz);
    bool hasChild = this->isChildMaskOn(n);
    if (!hasChild) {
        const bool active = this->isValueMaskOn(n);
        if (active || !math::isExactlyEqual(mNodes[n].getValue(), value)) {
            // If the voxel belongs to a tile that is either active or that
            // has a constant value that is different from the one provided,
            // a child subtree must be constructed.
            mChildMask.setOn(n); // we're adding a child node so set the mask on
            mValueMask.setOff(n); // value mask is always off if child mask is on
            hasChild = true;
            mNodes[n].setChild(new ChildNodeType(xyz, mNodes[n].getValue(), active));
        }
    }
    if (hasChild) {
        ChildT* child = mNodes[n].getChild();
        acc.insert(xyz, child);
        child->setValueOffAndCache(xyz, value, acc);
    }
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::setValueOn(const Coord& xyz, const ValueType& value)
{
    const Index n = this->coord2offset(xyz);
    bool hasChild = this->isChildMaskOn(n);
    if (!hasChild) {
        const bool active = this->isValueMaskOn(n); // tile's active state
        if (!active || !math::isExactlyEqual(mNodes[n].getValue(), value)) {
            // If the voxel belongs to a tile that is either inactive or that
            // has a constant value that is different from the one provided,
            // a child subtree must be constructed.
            mChildMask.setOn(n); // we're adding a child node so set the mask on
            mValueMask.setOff(n); // value mask is always off if child mask is on
            hasChild = true;
            mNodes[n].setChild(new ChildNodeType(xyz, mNodes[n].getValue(), active));
        }
    }
    if (hasChild) mNodes[n].getChild()->setValueOn(xyz, value);
}

template<typename ChildT, Index Log2Dim>
template<typename AccessorT>
inline void
InternalNode<ChildT, Log2Dim>::setValueAndCache(const Coord& xyz,
    const ValueType& value, AccessorT& acc)
{
    const Index n = this->coord2offset(xyz);
    bool hasChild = this->isChildMaskOn(n);
    if (!hasChild) {
        const bool active = this->isValueMaskOn(n);
        if (!active || !math::isExactlyEqual(mNodes[n].getValue(), value)) {
            // If the voxel belongs to a tile that is either inactive or that
            // has a constant value that is different from the one provided,
            // a child subtree must be constructed.
            mChildMask.setOn(n); // we're adding a child node so set the mask on
            mValueMask.setOff(n); // value mask is always off if child mask is on
            hasChild = true;
            mNodes[n].setChild(new ChildNodeType(xyz, mNodes[n].getValue(), active));
        }
    }
    if (hasChild) {
        acc.insert(xyz, mNodes[n].getChild());
        mNodes[n].getChild()->setValueAndCache(xyz, value, acc);
    }
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::setValueOnly(const Coord& xyz, const ValueType& value)
{
    const Index n = this->coord2offset(xyz);
    bool hasChild = this->isChildMaskOn(n);
    if (!hasChild && !math::isExactlyEqual(mNodes[n].getValue(), value)) {
        // If the voxel has a tile value that is different from the one provided,
        // a child subtree must be constructed.
        const bool active = this->isValueMaskOn(n);
        mChildMask.setOn(n); // we're adding a child node so set the mask on
        mValueMask.setOff(n); // value mask is always off if child mask is on
        hasChild = true;
        mNodes[n].setChild(new ChildNodeType(xyz, mNodes[n].getValue(), active));
    }
    if (hasChild) mNodes[n].getChild()->setValueOnly(xyz, value);
}

template<typename ChildT, Index Log2Dim>
template<typename AccessorT>
inline void
InternalNode<ChildT, Log2Dim>::setValueOnlyAndCache(const Coord& xyz,
                                                    const ValueType& value, AccessorT& acc)
{
    const Index n = this->coord2offset(xyz);
    bool hasChild = this->isChildMaskOn(n);
    if (!hasChild && !math::isExactlyEqual(mNodes[n].getValue(), value)) {
        // If the voxel has a tile value that is different from the one provided,
        // a child subtree must be constructed.
        const bool active = this->isValueMaskOn(n);
        mChildMask.setOn(n); // we're adding a child node so set the mask on
        mValueMask.setOff(n); // value mask is always off if child mask is on
        hasChild = true;
        mNodes[n].setChild(new ChildNodeType(xyz, mNodes[n].getValue(), active));
    }
    if (hasChild) {
        acc.insert(xyz, mNodes[n].getChild());
        mNodes[n].getChild()->setValueOnlyAndCache(xyz, value, acc);
    }
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::setActiveState(const Coord& xyz, bool on)
{
    const Index n = this->coord2offset(xyz);
    bool hasChild = this->isChildMaskOn(n);
    if (!hasChild) {
        if (on != this->isValueMaskOn(n)) {
            // If the voxel belongs to a tile with the wrong active state,
            // then a child subtree must be constructed.
            mChildMask.setOn(n); // we're adding a child node so set the mask on
            mValueMask.setOff(n); // value mask is always off if child mask is on
            mNodes[n].setChild(new ChildNodeType(xyz, mNodes[n].getValue(), !on));
                // 'on' is the voxel's new state, therefore '!on' is the tile's current state
            hasChild = true;
        }
    }
    if (hasChild) mNodes[n].getChild()->setActiveState(xyz, on);
}

template<typename ChildT, Index Log2Dim>
template<typename AccessorT>
inline void
InternalNode<ChildT, Log2Dim>::setActiveStateAndCache(const Coord& xyz, bool on, AccessorT& acc)
{
    const Index n = this->coord2offset(xyz);
    bool hasChild = this->isChildMaskOn(n);
    if (!hasChild) {
        if (on != this->isValueMaskOn(n)) {
            // If the voxel belongs to a tile with the wrong active state,
            // then a child subtree must be constructed.
            mChildMask.setOn(n); // we're adding a child node so set the mask on
            mValueMask.setOff(n); // value mask is always off if child mask is on
            mNodes[n].setChild(new ChildNodeType(xyz, mNodes[n].getValue(), !on));
                // 'on' is the voxel's new state, therefore '!on' is the tile's current state
            hasChild = true;
        }
    }
    if (hasChild) {
        ChildT* child = mNodes[n].getChild();
        acc.insert(xyz, child);
        child->setActiveStateAndCache(xyz, on, acc);
    }
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::setValuesOn()
{
    mValueMask = !mChildMask;
    for (ChildOnIter iter = this->beginChildOn(); iter; ++iter) {
        mNodes[iter.pos()].getChild()->setValuesOn();
    }
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::setValueOnMin(const Coord& xyz, const ValueType& value)
{
    const Index n = InternalNode::coord2offset(xyz);
    bool hasChild = this->isChildMaskOn(n);
    if (!hasChild) {
        const bool active = this->isValueMaskOn(n);
        if (!active || (mNodes[n].getValue() > value)) {
            // If the voxel belongs to a tile that is either inactive or that
            // has a constant value that is greater than the one provided,
            // a child subtree must be constructed.
            mChildMask.setOn(n); // we're adding a child node so set the mask on
            mValueMask.setOff(n); // value mask is always off if child mask is on
            hasChild = true;
            mNodes[n].setChild(new ChildNodeType(xyz, mNodes[n].getValue(), active));
        }
    }
    if (hasChild) mNodes[n].getChild()->setValueOnMin(xyz, value);
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::setValueOnMax(const Coord& xyz, const ValueType& value)
{
    const Index n = InternalNode::coord2offset(xyz);
    bool hasChild = this->isChildMaskOn(n);
    if (!hasChild) {
        const bool active = this->isValueMaskOn(n);
        if (!active || (value > mNodes[n].getValue())) {
            // If the voxel belongs to a tile that is either inactive or that
            // has a constant value that is less than the one provided,
            // a child subtree must be constructed.
            mChildMask.setOn(n); // we're adding a child node so set the mask on
            mValueMask.setOff(n); // value mask is always off if child mask is on
            hasChild = true;
            mNodes[n].setChild(new ChildNodeType(xyz, mNodes[n].getValue(), active));
        }
    }
    if (hasChild) mNodes[n].getChild()->setValueOnMax(xyz, value);
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::setValueOnSum(const Coord& xyz, const ValueType& addend)
{
    const Index n = InternalNode::coord2offset(xyz);
    bool hasChild = this->isChildMaskOn(n);
    if (!hasChild) {
        const bool active = this->isValueMaskOn(n);
        if (!active || !math::isExactlyEqual(addend, zeroVal<ValueType>())) {
            // If the voxel belongs to a tile that is inactive or if
            // the addend is nonzero, a child subtree must be constructed.
            mChildMask.setOn(n);//  we're adding a child node so set the mask on
            mValueMask.setOff(n);// value mask is always off if child mask is on
            hasChild = true;
            mNodes[n].setChild(new ChildNodeType(xyz, mNodes[n].getValue(), active));
        }
    }
    if (hasChild) mNodes[n].getChild()->setValueOnSum(xyz, addend);
}

template<typename ChildT, Index Log2Dim>
template<typename AccessorT>
inline void
InternalNode<ChildT, Log2Dim>::setValueOnSumAndCache(const Coord& xyz,
    const ValueType& addend, AccessorT& acc)
{
    const Index n = this->coord2offset(xyz);
    bool hasChild = this->isChildMaskOn(n);
    if (!hasChild) {
        const bool active = this->isValueMaskOn(n);
        if (!active || !math::isExactlyEqual(addend, zeroVal<ValueType>())) {
            // If the voxel belongs to a tile that is inactive or if
            // the addend is nonzero, a child subtree must be constructed.
            mChildMask.setOn(n); // we're adding a child node so set the mask on
            mValueMask.setOff(n); // value mask is always off if child mask is on
            hasChild = true;
            mNodes[n].setChild(new ChildNodeType(xyz, mNodes[n].getValue(), active));
        }
    }
    if (hasChild) {
        acc.insert(xyz, mNodes[n].getChild());
        mNodes[n].getChild()->setValueOnSumAndCache(xyz, addend, acc);
    }
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::fill(const CoordBBox& bbox, const ValueType& value, bool active)
{
    Coord xyz, tileMin, tileMax;
    for (int x = bbox.min().x(); x <= bbox.max().x(); x = tileMax.x() + 1) {
        xyz.setX(x);
        for (int y = bbox.min().y(); y <= bbox.max().y(); y = tileMax.y() + 1) {
            xyz.setY(y);
            for (int z = bbox.min().z(); z <= bbox.max().z(); z = tileMax.z() + 1) {
                xyz.setZ(z);

                // Get the bounds of the tile that contains voxel (x, y, z).
                const Index n = this->coord2offset(xyz);
                tileMin = this->offset2globalCoord(n);
                tileMax = tileMin.offsetBy(ChildT::DIM - 1);

                if (xyz != tileMin || Coord::lessThan(bbox.max(), tileMax)) {
                    // If the box defined by (xyz, bbox.max()) doesn't completely enclose
                    // the tile to which xyz belongs, create a child node (or retrieve
                    // the existing one).
                    ChildT* child = NULL;
                    if (this->isChildMaskOff(n)) {
                        // Replace the tile with a newly-created child that is initialized
                        // with the tile's value and active state.
                        child = new ChildT(xyz, mNodes[n].getValue(), this->isValueMaskOn(n));
                        mChildMask.setOn(n);
                        mValueMask.setOff(n);
                        mNodes[n].setChild(child);
                    } else {
                        child = mNodes[n].getChild();
                    }

                    // Forward the fill request to the child.
                    if (child) {
                        child->fill(CoordBBox(xyz, Coord::minComponent(bbox.max(), tileMax)),
                            value, active);
                    }

                } else {
                    // If the box given by (xyz, bbox.max()) completely encloses
                    // the tile to which xyz belongs, create the tile (if it
                    // doesn't already exist) and give it the fill value.
                    this->makeChildNodeEmpty(n, value);
                    mValueMask.set(n, active);
                }
            }
        }
    }
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
template<typename DenseT>
inline void
InternalNode<ChildT, Log2Dim>::copyToDense(const CoordBBox& bbox, DenseT& dense) const
{
    const size_t xStride = dense.xStride(), yStride = dense.yStride();// zStride=1
    const Coord& min = dense.bbox().min();
    for (Coord xyz = bbox.min(), max; xyz[0] <= bbox.max()[0]; xyz[0] = max[0] + 1) {
        for (xyz[1] = bbox.min()[1]; xyz[1] <= bbox.max()[1]; xyz[1] = max[1] + 1) {
            for (xyz[2] = bbox.min()[2]; xyz[2] <= bbox.max()[2]; xyz[2] = max[2] + 1) {
                const Index n = this->coord2offset(xyz);
                // Get max coordinates of the child node that contains voxel xyz.
                max = this->offset2globalCoord(n).offsetBy(ChildT::DIM-1);

                // Get the bbox of the interection of bbox and the child node
                CoordBBox sub(xyz, Coord::minComponent(bbox.max(), max));

                if (this->isChildMaskOn(n)) {//is a child
                    mNodes[n].getChild()->copyToDense(sub, dense);
                } else {//a tile value
                    const ValueType value = mNodes[n].getValue();
                    sub.translate(-min);
                    ValueType* a0 = dense.data() + sub.min()[2];
                    for (Int32 x=sub.min()[0], ex=sub.max()[0]+1; x<ex; ++x) {
                        ValueType* a1 = a0 + x*xStride;
                        for (Int32 y=sub.min()[1], ey=sub.max()[1]+1; y<ey; ++y) {
                            ValueType* a2 = a1 + y*yStride;
                            for (Int32 z=sub.min()[2], ez=sub.max()[2]+1; z<ez; ++z) *a2++ = value;
                        }
                    }
                }
            }
        }
    }
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::writeTopology(std::ostream& os, bool toHalf) const
{
    mChildMask.save(os);
    mValueMask.save(os);

    {
        // Copy all of this node's values into an array.
        boost::shared_array<ValueType> values(new ValueType[NUM_VALUES]);
        const ValueType zero = zeroVal<ValueType>();
        for (Index i = 0; i < NUM_VALUES; ++i) {
            values[i] = (mChildMask.isOff(i) ? mNodes[i].getValue() : zero);
        }
        // Compress (optionally) and write out the contents of the array.
        io::writeCompressedValues(os, values.get(), NUM_VALUES, mValueMask, mChildMask, toHalf);
    }
    // Write out the child nodes in order.
    for (ChildOnCIter iter = this->cbeginChildOn(); iter; ++iter) {
        iter->writeTopology(os, toHalf);
    }
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::readTopology(std::istream& is, bool fromHalf)
{
    mChildMask.load(is);
    mValueMask.load(is);

    if (io::getFormatVersion(is) < OPENVDB_FILE_VERSION_INTERNALNODE_COMPRESSION) {
        for (Index i = 0; i < NUM_VALUES; ++i) {
            if (this->isChildMaskOn(i)) {
                ChildNodeType* child =
                    new ChildNodeType(offset2globalCoord(i), zeroVal<ValueType>());
                mNodes[i].setChild(child);
                child->readTopology(is);
            } else {
                ValueType value;
                is.read(reinterpret_cast<char*>(&value), sizeof(ValueType));
                mNodes[i].setValue(value);
            }
        }
    } else {
        const bool oldVersion =
            (io::getFormatVersion(is) < OPENVDB_FILE_VERSION_NODE_MASK_COMPRESSION);
        const Index numValues = (oldVersion ? mChildMask.countOff() : NUM_VALUES);
        {
            // Read in (and uncompress, if necessary) all of this node's values
            // into a contiguous array.
            boost::shared_array<ValueType> values(new ValueType[numValues]);
            io::readCompressedValues(is, values.get(), numValues, mValueMask, fromHalf);

            // Copy values from the array into this node's table.
            if (oldVersion) {
                Index n = 0;
                for (ValueAllIter iter = this->beginValueAll(); iter; ++iter) {
                    mNodes[iter.pos()].setValue(values[n++]);
                }
                assert(n == numValues);
            } else {
                for (ValueAllIter iter = this->beginValueAll(); iter; ++iter) {
                    mNodes[iter.pos()].setValue(values[iter.pos()]);
                }
            }
        }
        // Read in all child nodes and insert them into the table at their proper locations.
        for (ChildOnIter iter = this->beginChildOn(); iter; ++iter) {
            ChildNodeType* child = new ChildNodeType(iter.getCoord(), zeroVal<ValueType>());
            mNodes[iter.pos()].setChild(child);
            child->readTopology(is, fromHalf);
        }
    }
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
inline const typename ChildT::ValueType&
InternalNode<ChildT, Log2Dim>::getFirstValue() const
{
    return (this->isChildMaskOn(0) ? mNodes[0].getChild()->getFirstValue() : mNodes[0].getValue());
}


template<typename ChildT, Index Log2Dim>
inline const typename ChildT::ValueType&
InternalNode<ChildT, Log2Dim>::getLastValue() const
{
    const Index n = NUM_VALUES - 1;
    return (this->isChildMaskOn(n) ? mNodes[n].getChild()->getLastValue() : mNodes[n].getValue());
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::signedFloodFill(const ValueType& background)
{
    this->signedFloodFill(background, negative(background));
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::signedFloodFill(const ValueType& outsideValue,
                                               const ValueType& insideValue)
{
    // First, flood fill all child nodes.
    for (ChildOnIter iter = this->beginChildOn(); iter; ++iter) {
        iter->signedFloodFill(outsideValue, insideValue);
    }
    const Index first = mChildMask.findFirstOn();
    if (first < NUM_VALUES) {
        bool xInside = math::isNegative(mNodes[first].getChild()->getFirstValue()),
            yInside = xInside, zInside = xInside;
        for (Index x = 0; x != (1 << Log2Dim); ++x) {
            const int x00 = x << (2 * Log2Dim); // offset for block(x, 0, 0)
            if (isChildMaskOn(x00)) {
                xInside = math::isNegative(mNodes[x00].getChild()->getLastValue());
            }
            yInside = xInside;
            for (Index y = 0; y != (1 << Log2Dim); ++y) {
                const Index xy0 = x00 + (y << Log2Dim); // offset for block(x, y, 0)
                if (isChildMaskOn(xy0)) {
                    yInside = math::isNegative(mNodes[xy0].getChild()->getLastValue());
                }
                zInside = yInside;
                for (Index z = 0; z != (1 << Log2Dim); ++z) {
                    const Index xyz = xy0 + z; // offset for block(x, y, z)
                    if (isChildMaskOn(xyz)) {
                        zInside = math::isNegative(mNodes[xyz].getChild()->getLastValue());
                    } else {
                        mNodes[xyz].setValue(zInside ? insideValue : outsideValue);
                    }
                }
            }
        }
    } else {//no child nodes exist simply use the sign of the first tile value.
        const ValueType v =  math::isNegative(mNodes[0].getValue()) ? insideValue : outsideValue;
        for (Index i = 0; i < NUM_VALUES; ++i) mNodes[i].setValue(v);
    }
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::negate()
{
    for (Index i = 0; i < NUM_VALUES; ++i) {
        if (this->isChildMaskOn(i)) {
            mNodes[i].getChild()->negate();
        } else {
            mNodes[i].setValue(negative(mNodes[i].getValue()));
        }
    }

}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::voxelizeActiveTiles()
{
    for (ValueOnIter iter = this->beginValueOn(); iter; ++iter) {
        const Index n = iter.pos();
        ChildNodeType* child = new ChildNodeType(iter.getCoord(), iter.getValue(), true);
        mValueMask.setOff(n);
        mChildMask.setOn(n);
        mNodes[n].setChild(child);
    }
    for (ChildOnIter iter = this->beginChildOn(); iter; ++iter) iter->voxelizeActiveTiles();
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::merge(InternalNode& other,
    const ValueType& background, const ValueType& otherBackground)
{
    for (ChildOnIter iter = other.beginChildOn(); iter; ++iter) {
        const Index n = iter.pos();
        if (mChildMask.isOff(n)) { // transfer node
            ChildNodeType* child = other.mNodes[n].getChild();
            other.mChildMask.setOff(n);
            // Note other's new tile value is undefined which is okay since
            // other is assumed to be cannibalized in the process of merging!
            child->resetBackground(otherBackground, background);
            mChildMask.setOn(n);
            mValueMask.setOff(n);
            mNodes[n].setChild(child);
        } else {
            mNodes[n].getChild()->merge(*iter, background, otherBackground);
        }
    }

    // Copy active tile values.
    for (ValueOnCIter iter = other.cbeginValueOn(); iter; ++iter) {
        const Index n = iter.pos();
        if (mChildMask.isOff(n) && mValueMask.isOff(n)) {
            mNodes[n].setValue(iter.getValue());
            mValueMask.setOn(n);
        }
    }
}


template<typename ChildT, Index Log2Dim>
template<typename OtherChildT>
inline void
InternalNode<ChildT, Log2Dim>::topologyUnion(const InternalNode<OtherChildT, Log2Dim>& other)
{
    typedef typename InternalNode<OtherChildT, Log2Dim>::ChildOnCIter OtherChildIter;
    typedef typename InternalNode<OtherChildT, Log2Dim>::ValueOnCIter OtherValueIter;

    for (OtherChildIter iter = other.cbeginChildOn(); iter; ++iter) {
        const Index i = iter.pos();
        if (mChildMask.isOn(i)) {//this has a child node
            mNodes[i].getChild()->topologyUnion(*iter);
        } else {// this is a tile so replace it with a child branch with identical topology
            ChildNodeType* child = new ChildNodeType(*iter, mNodes[i].getValue(), TopologyCopy());
            if (mValueMask.isOn(i)) {
                mValueMask.isOff(i);//we're replacing the active tile with a child branch
                child->setValuesOn();//activate all values since it was an active tile
            }
            mChildMask.setOn(i);
            mNodes[i].setChild(child);
        }
    }
    for (OtherValueIter iter = other.cbeginValueOn(); iter; ++iter) {
        const Index i = iter.pos();
        if (mChildMask.isOn(i)) {
            mNodes[i].getChild()->setValuesOn();
        } else if (mValueMask.isOff(i)) { //inactive tile
            mValueMask.setOn(i);
        }
    }
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
template<typename CombineOp>
inline void
InternalNode<ChildT, Log2Dim>::combine(InternalNode& other, CombineOp& op)
{
    const ValueType zero = zeroVal<ValueType>();

    CombineArgs<ValueType> args;

    for (Index i = 0; i < NUM_VALUES; ++i) {
        if (this->isChildMaskOff(i) && other.isChildMaskOff(i)) {
            // Both this node and the other node have constant values (tiles).
            // Combine the two values and store the result as this node's new tile value.
            op(args.setARef(mNodes[i].getValue())
                .setAIsActive(isValueMaskOn(i))
                .setBRef(other.mNodes[i].getValue())
               .setBIsActive(other.isValueMaskOn(i)));
            mNodes[i].setValue(args.result());
            mValueMask.set(i, args.resultIsActive());
        } else if (this->isChildMaskOn(i) && other.isChildMaskOff(i)) {
            // Combine this node's child with the other node's constant value.
            ChildNodeType* child = mNodes[i].getChild();
            assert(child);
            if (child) {
                child->combine(other.mNodes[i].getValue(), other.isValueMaskOn(i), op);
            }
        } else if (this->isChildMaskOff(i) && other.isChildMaskOn(i)) {
            // Combine this node's constant value with the other node's child.
            ChildNodeType* child = other.mNodes[i].getChild();
            assert(child);
            if (child) {
                // Combine this node's constant value with the other node's child,
                // but use a new functor in which the A and B values are swapped,
                // since the constant value is the A value, not the B value.
                SwappedCombineOp<ValueType, CombineOp> swappedOp(op);
                child->combine(mNodes[i].getValue(), isValueMaskOn(i), swappedOp);

                // Steal the other node's child.
                other.mChildMask.setOff(i);
                other.mNodes[i].setValue(zero);
                mChildMask.setOn(i);
                mValueMask.setOff(i);
                mNodes[i].setChild(child);
            }

        } else /*if (isChildMaskOn(i) && other.isChildMaskOn(i))*/ {
            // Combine this node's child with the other node's child.
            ChildNodeType
                *child = mNodes[i].getChild(),
                *otherChild = other.mNodes[i].getChild();
            assert(child);
            assert(otherChild);
            if (child && otherChild) {
                child->combine(*otherChild, op);
            }
        }
    }
}


template<typename ChildT, Index Log2Dim>
template<typename CombineOp>
inline void
InternalNode<ChildT, Log2Dim>::combine(const ValueType& value, bool valueIsActive, CombineOp& op)
{
    CombineArgs<ValueType> args;

    for (Index i = 0; i < NUM_VALUES; ++i) {
        if (this->isChildMaskOff(i)) {
            // Combine this node's constant value with the given constant value.
            op(args.setARef(mNodes[i].getValue())
               .setAIsActive(isValueMaskOn(i))
               .setBRef(value)
               .setBIsActive(valueIsActive));
            mNodes[i].setValue(args.result());
            mValueMask.set(i, args.resultIsActive());
        } else /*if (isChildMaskOn(i))*/ {
            // Combine this node's child with the given constant value.
            ChildNodeType* child = mNodes[i].getChild();
            assert(child);
            if (child) child->combine(value, valueIsActive, op);
        }
    }
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
template<typename CombineOp>
inline void
InternalNode<ChildT, Log2Dim>::combine2(const InternalNode& other0, const InternalNode& other1,
    CombineOp& op)
{
    CombineArgs<ValueType> args;

    for (Index i = 0; i < NUM_VALUES; ++i) {
        if (other0.isChildMaskOff(i) && other1.isChildMaskOff(i)) {
            op(args.setARef(other0.mNodes[i].getValue())
                .setAIsActive(other0.isValueMaskOn(i))
                .setBRef(other1.mNodes[i].getValue())
                .setBIsActive(other1.isValueMaskOn(i)));
            // Replace child i with a constant value.
            this->makeChildNodeEmpty(i, args.result());
            mValueMask.set(i, args.resultIsActive());
        } else {
            ChildNodeType* otherChild = other0.isChildMaskOn(i)
                ? other0.mNodes[i].getChild() : other1.mNodes[i].getChild();
            assert(otherChild);
            if (this->isChildMaskOff(i)) {
                // Add a new child with the same coordinates, etc.
                // as the other node's child.
                mChildMask.setOn(i);
                mValueMask.setOff(i);
                mNodes[i].setChild(new ChildNodeType(otherChild->getOrigin(),
                                                     mNodes[i].getValue()));
            }

            if (other0.isChildMaskOff(i)) {
                // Combine node1's child with node0's constant value
                // and write the result into child i.
                mNodes[i].getChild()->combine2(other0.mNodes[i].getValue(),
                    *other1.mNodes[i].getChild(), other0.isValueMaskOn(i), op);
            } else if (other1.isChildMaskOff(i)) {
                // Combine node0's child with node1's constant value
                // and write the result into child i.
                mNodes[i].getChild()->combine2(*other0.mNodes[i].getChild(),
                    other1.mNodes[i].getValue(), other1.isValueMaskOn(i), op);
            } else {
                // Combine node0's child with node1's child
                // and write the result into child i.
                mNodes[i].getChild()->combine2(*other0.mNodes[i].getChild(),
                    *other1.mNodes[i].getChild(), op);
            }
        }
    }
}


template<typename ChildT, Index Log2Dim>
template<typename CombineOp>
inline void
InternalNode<ChildT, Log2Dim>::combine2(const ValueType& value, const InternalNode& other,
    bool valueIsActive, CombineOp& op)
{
    CombineArgs<ValueType> args;

    for (Index i = 0; i < NUM_VALUES; ++i) {
        if (other.isChildMaskOff(i)) {
            op(args.setARef(value)
                .setAIsActive(valueIsActive)
                .setBRef(other.mNodes[i].getValue())
                .setBIsActive(other.isValueMaskOn(i)));
            // Replace child i with a constant value.
            this->makeChildNodeEmpty(i, args.result());
            mValueMask.set(i, args.resultIsActive());
        } else {
            ChildNodeType* otherChild = other.mNodes[i].getChild();
            assert(otherChild);
            if (this->isChildMaskOff(i)) {
                // Add a new child with the same coordinates, etc.
                // as the other node's child.
                /// @todo Could the other node's ChildNodeType be different from this node's?
                mChildMask.setOn(i);
                mValueMask.setOff(i);
                mNodes[i].setChild(new ChildNodeType(*otherChild));
            }
            // Combine the other node's child with a constant value
            // and write the result into child i.
            mNodes[i].getChild()->combine2(value, *otherChild, valueIsActive, op);
        }
    }
}


template<typename ChildT, Index Log2Dim>
template<typename CombineOp>
inline void
InternalNode<ChildT, Log2Dim>::combine2(const InternalNode& other, const ValueType& value,
    bool valueIsActive, CombineOp& op)
{
    CombineArgs<ValueType> args;

    for (Index i = 0; i < NUM_VALUES; ++i) {
        if (other.isChildMaskOff(i)) {
            op(args.setARef(other.mNodes[i].getValue())
                .setAIsActive(other.isValueMaskOn(i))
                .setBRef(value)
                .setBIsActive(valueIsActive));
            // Replace child i with a constant value.
            this->makeChildNodeEmpty(i, args.result());
            mValueMask.set(i, args.resultIsActive());
        } else {
            ChildNodeType* otherChild = other.mNodes[i].getChild();
            assert(otherChild);
            if (this->isChildMaskOff(i)) {
                // Add a new child with the same coordinates, etc.
                // as the other node's child.
                mChildMask.setOn(i);
                mValueMask.setOff(i);
                mNodes[i].setChild(new ChildNodeType(otherChild->getOrigin(),
                                                     mNodes[i].getValue()));
            }
            // Combine the other node's child with a constant value
            // and write the result into child i.
            mNodes[i].getChild()->combine2(*otherChild, value, valueIsActive, op);
        }
    }
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
template<typename BBoxOp>
inline void
InternalNode<ChildT, Log2Dim>::visitActiveBBox(BBoxOp& op) const
{
    for (ValueOnCIter i = this->cbeginValueOn(); i; ++i) {
#ifdef _MSC_VER
        op.operator()<LEVEL>(CoordBBox::createCube(i.getCoord(), ChildNodeType::DIM));
#else
        op.template operator()<LEVEL>(CoordBBox::createCube(i.getCoord(), ChildNodeType::DIM));
#endif
    }
    if (op.template descent<LEVEL>()) {
        for (ChildOnCIter i = this->cbeginChildOn(); i; ++i) i->visitActiveBBox(op);
    } else {
        for (ChildOnCIter i = this->cbeginChildOn(); i; ++i) {
#ifdef _MSC_VER
            op.operator()<LEVEL>(i->getNodeBoundingBox());
#else
            op.template operator()<LEVEL>(i->getNodeBoundingBox());
#endif
        }
    }
}


template<typename ChildT, Index Log2Dim>
template<typename VisitorOp>
inline void
InternalNode<ChildT, Log2Dim>::visit(VisitorOp& op)
{
    doVisit<InternalNode, VisitorOp, ChildAllIter>(*this, op);
}


template<typename ChildT, Index Log2Dim>
template<typename VisitorOp>
inline void
InternalNode<ChildT, Log2Dim>::visit(VisitorOp& op) const
{
    doVisit<const InternalNode, VisitorOp, ChildAllCIter>(*this, op);
}


template<typename ChildT, Index Log2Dim>
template<typename NodeT, typename VisitorOp, typename ChildAllIterT>
inline void
InternalNode<ChildT, Log2Dim>::doVisit(NodeT& self, VisitorOp& op)
{
    typename NodeT::ValueType val;
    for (ChildAllIterT iter = self.beginChildAll(); iter; ++iter) {
        if (op(iter)) continue;
        if (typename ChildAllIterT::ChildNodeType* child = iter.probeChild(val)) {
            child->visit(op);
        }
    }
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
template<typename OtherNodeType, typename VisitorOp>
inline void
InternalNode<ChildT, Log2Dim>::visit2Node(OtherNodeType& other, VisitorOp& op)
{
    doVisit2Node<InternalNode, OtherNodeType, VisitorOp, ChildAllIter,
        typename OtherNodeType::ChildAllIter>(*this, other, op);
}


template<typename ChildT, Index Log2Dim>
template<typename OtherNodeType, typename VisitorOp>
inline void
InternalNode<ChildT, Log2Dim>::visit2Node(OtherNodeType& other, VisitorOp& op) const
{
    doVisit2Node<const InternalNode, OtherNodeType, VisitorOp, ChildAllCIter,
        typename OtherNodeType::ChildAllCIter>(*this, other, op);
}


template<typename ChildT, Index Log2Dim>
template<
    typename NodeT,
    typename OtherNodeT,
    typename VisitorOp,
    typename ChildAllIterT,
    typename OtherChildAllIterT>
inline void
InternalNode<ChildT, Log2Dim>::doVisit2Node(NodeT& self, OtherNodeT& other, VisitorOp& op)
{
    // Allow the two nodes to have different ValueTypes, but not different dimensions.
    BOOST_STATIC_ASSERT(OtherNodeT::NUM_VALUES == NodeT::NUM_VALUES);
    BOOST_STATIC_ASSERT(OtherNodeT::LEVEL == NodeT::LEVEL);

    typename NodeT::ValueType val;
    typename OtherNodeT::ValueType otherVal;

    ChildAllIterT iter = self.beginChildAll();
    OtherChildAllIterT otherIter = other.beginChildAll();

    for ( ; iter && otherIter; ++iter, ++otherIter)
    {
        const size_t skipBranch = static_cast<size_t>(op(iter, otherIter));

        typename ChildAllIterT::ChildNodeType* child =
            (skipBranch & 1U) ? NULL : iter.probeChild(val);
        typename OtherChildAllIterT::ChildNodeType* otherChild =
            (skipBranch & 2U) ? NULL : otherIter.probeChild(otherVal);

        if (child != NULL && otherChild != NULL) {
            child->visit2Node(*otherChild, op);
        } else if (child != NULL) {
            child->visit2(otherIter, op);
        } else if (otherChild != NULL) {
            otherChild->visit2(iter, op, /*otherIsLHS=*/true);
        }
    }
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
template<typename OtherChildAllIterType, typename VisitorOp>
inline void
InternalNode<ChildT, Log2Dim>::visit2(OtherChildAllIterType& otherIter,
    VisitorOp& op, bool otherIsLHS)
{
    doVisit2<InternalNode, VisitorOp, ChildAllIter, OtherChildAllIterType>(
        *this, otherIter, op, otherIsLHS);
}


template<typename ChildT, Index Log2Dim>
template<typename OtherChildAllIterType, typename VisitorOp>
inline void
InternalNode<ChildT, Log2Dim>::visit2(OtherChildAllIterType& otherIter,
    VisitorOp& op, bool otherIsLHS) const
{
    doVisit2<const InternalNode, VisitorOp, ChildAllCIter, OtherChildAllIterType>(
        *this, otherIter, op, otherIsLHS);
}


template<typename ChildT, Index Log2Dim>
template<typename NodeT, typename VisitorOp, typename ChildAllIterT, typename OtherChildAllIterT>
inline void
InternalNode<ChildT, Log2Dim>::doVisit2(NodeT& self, OtherChildAllIterT& otherIter,
    VisitorOp& op, bool otherIsLHS)
{
    if (!otherIter) return;

    const size_t skipBitMask = (otherIsLHS ? 2U : 1U);

    typename NodeT::ValueType val;
    for (ChildAllIterT iter = self.beginChildAll(); iter; ++iter) {
        const size_t skipBranch = static_cast<size_t>(
            otherIsLHS ? op(otherIter, iter) : op(iter, otherIter));

        typename ChildAllIterT::ChildNodeType* child =
            (skipBranch & skipBitMask) ? NULL : iter.probeChild(val);

        if (child != NULL) child->visit2(otherIter, op, otherIsLHS);
    }
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::writeBuffers(std::ostream& os, bool toHalf) const
{
    for (ChildOnCIter iter = this->cbeginChildOn(); iter; ++iter) {
        iter->writeBuffers(os, toHalf);
    }
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::readBuffers(std::istream& is, bool fromHalf)
{
    for (ChildOnIter iter = this->beginChildOn(); iter; ++iter) {
        iter->readBuffers(is, fromHalf);
    }
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
void
InternalNode<ChildT, Log2Dim>::getNodeLog2Dims(std::vector<Index>& dims)
{
    dims.push_back(Log2Dim);
    ChildNodeType::getNodeLog2Dims(dims);
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::offset2coord(Index n, Coord &xyz)
{
    assert(n<(1<<3*Log2Dim));
    xyz.setX(n >> 2*Log2Dim);
    n &= ((1<<2*Log2Dim)-1);
    xyz.setY(n >> Log2Dim);
    xyz.setZ(n & ((1<<Log2Dim)-1));
}


template<typename ChildT, Index Log2Dim>
inline Index
InternalNode<ChildT, Log2Dim>::coord2offset(const Coord& xyz)
{
    return (((xyz[0]&DIM-1u)>>ChildNodeType::TOTAL)<<2*Log2Dim)
        +  (((xyz[1]&DIM-1u)>>ChildNodeType::TOTAL)<<  Log2Dim)
        +   ((xyz[2]&DIM-1u)>>ChildNodeType::TOTAL);
}


template<typename ChildT, Index Log2Dim>
inline Coord
InternalNode<ChildT, Log2Dim>::offset2globalCoord(Index n) const
{
    Coord local;
    this->offset2coord(n, local);
    local <<= ChildT::TOTAL;
    return local + this->getOrigin();
}


////////////////////////////////////////


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::resetBackground(const ValueType& oldBackground,
                                               const ValueType& newBackground)
{
    if (math::isExactlyEqual(oldBackground, newBackground)) return;
    for (Index i = 0; i < NUM_VALUES; ++i) {
       if (this->isChildMaskOn(i)) {
           mNodes[i].getChild()->resetBackground(oldBackground, newBackground);
       } else if (this->isValueMaskOff(i)) {
           if (math::isApproxEqual(mNodes[i].getValue(), oldBackground)) {
               mNodes[i].setValue(newBackground);
           } else if (math::isApproxEqual(mNodes[i].getValue(), negative(oldBackground))) {
               mNodes[i].setValue(negative(newBackground));
           }
       }
    }
}


template<typename ChildT, Index Log2Dim>
template<typename OtherChildNodeType, Index OtherLog2Dim>
inline bool
InternalNode<ChildT, Log2Dim>::hasSameTopology(
    const InternalNode<OtherChildNodeType, OtherLog2Dim>* other) const
{
    if (Log2Dim != OtherLog2Dim || mChildMask != other->mChildMask ||
        mValueMask != other->mValueMask) return false;
    for (ChildOnCIter iter = this->cbeginChildOn(); iter; ++iter) {
        if (!iter->hasSameTopology(other->mNodes[iter.pos()].getChild())) return false;
    }
    return true;
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::setChildNode(Index i, ChildNodeType* child)
{
    assert(child);
    if (this->isChildMaskOn(i)) {
        delete mNodes[i].getChild();
    } else {
        mChildMask.setOn(i);
        mValueMask.setOff(i);
    }
    mNodes[i].setChild(child);
}


template<typename ChildT, Index Log2Dim>
inline ChildT*
InternalNode<ChildT, Log2Dim>::unsetChildNode(Index i, const ValueType& value)
{
    if (this->isChildMaskOff(i)) {
        mNodes[i].setValue(value);
        return NULL;
    }
    ChildNodeType* child = mNodes[i].getChild();
    mChildMask.setOff(i);
    mNodes[i].setValue(value);
    return child;
}


template<typename ChildT, Index Log2Dim>
inline void
InternalNode<ChildT, Log2Dim>::makeChildNodeEmpty(Index n, const ValueType& value)
{
    delete this->unsetChildNode(n, value);
}

template<typename ChildT, Index Log2Dim>
inline ChildT*
InternalNode<ChildT, Log2Dim>::getChildNode(Index n)
{
    return (this->isChildMaskOn(n) ? mNodes[n].getChild() : NULL);
}


template<typename ChildT, Index Log2Dim>
inline const ChildT*
InternalNode<ChildT, Log2Dim>::getChildNode(Index n) const
{
    return (this->isChildMaskOn(n) ? mNodes[n].getChild() : NULL);
}

} // namespace tree
} // namespace OPENVDB_VERSION_NAME
} // namespace openvdb

#endif // OPENVDB_TREE_INTERNALNODE_HAS_BEEN_INCLUDED

// Copyright (c) 2012-2013 DreamWorks Animation LLC
// All rights reserved. This software is distributed under the
// Mozilla Public License 2.0 ( http://www.mozilla.org/MPL/2.0/ )
