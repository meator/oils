// gc_heap.cc

#include "gc_heap.h"

using gc_heap::Cell;
using gc_heap::Heap;
using gc_heap::Local;

namespace gc_heap {

Heap gHeap;

// LayoutForwarded and LayoutFixed aren't real types.  You can cast arbitrary
// cells to them to access a HOMOGENEOUS REPRESENTATION useful for garbage
// collection.

class LayoutForwarded : public Cell {
 public:
  Cell* new_location;  // valid if and only if heap_tag == Tag::Forwarded
};

// for Tag::FixedSize
class LayoutFixed : public Cell {
 public:
  Cell* children_[16];  // only the entries denoted in field_mask will be valid
};

// Move an object from one space to another.
Cell* Heap::Relocate(Cell* cell) {
  // note: femtolisp has ismanaged() in addition to isforwarded()
  // ismanaged() could be for globals

  // it handles TAG_CONS, TAG_VECTOR, TAG_CPRIM, TAG_CVALUE, (we might want
  // this), TAG_FUNCTION, TAG_SYM
  //
  // We have fewer cases than that.  We just use a Cell.

  if (cell->heap_tag == Tag::Forwarded) {
    auto f = reinterpret_cast<LayoutForwarded*>(cell);
    return f->new_location;
  } else {
    auto new_location = reinterpret_cast<Cell*>(free_);
    // Note: if we wanted to save space on ASDL records, we could calculate
    // their length from the field_mask here.  How much would it slow down GC?
    int n = cell->cell_len_;
    memcpy(new_location, cell, n);
    free_ += n;

#if GC_DEBUG
    num_live_cells_++;
#endif

    auto f = reinterpret_cast<LayoutForwarded*>(cell);
    f->heap_tag = Tag::Forwarded;
    f->new_location = new_location;
    return new_location;
  }
}

void Heap::Collect() {
  log("--> COLLECT");

  // Copy policy from femtolisp for now:
  //
  // If we're using > 80% of the space, resize tospace so we have more space to
  // fill next time. if we grew tospace last time, grow the other half of the
  // heap this time to catch up.

  scan_ = to_space_;  // boundary between black and gray
  free_ = to_space_;  // where to copy new entries

#if GC_DEBUG
  num_live_cells_ = 0;
#endif

  for (int i = 0; i < roots_top_; ++i) {
    auto handle = static_cast<Local<void>*>(roots_[i]);
    auto root = reinterpret_cast<Cell*>(handle->Get());

    log("%d. handle %p", i, handle);
    log("     root %p", root);

    // This updates the underlying Str/List/Dict with a forwarding pointer,
    // i.e. for other objects that are pointing to it
    Cell* new_location = Relocate(root);

    // This update is for the "double indirection", so future accesses to a
    // local variable use the new location
    handle->Update(new_location);
  }

  while (scan_ < free_) {
    auto cell = reinterpret_cast<Cell*>(scan_);
    switch (cell->heap_tag) {
    case Tag::FixedSize: {
      auto fixed = reinterpret_cast<LayoutFixed*>(cell);
      int mask = fixed->field_mask_;
      for (int i = 0; i < 16; ++i) {
        if (mask & (1 << i)) {
          Cell* child = fixed->children_[i];
          // log("i = %d, p = %p, heap_tag = %d", i, child, child->heap_tag);
          if (child) {
            fixed->children_[i] = Relocate(child);
          }
        }
      }
      break;
    }
    case Tag::Scanned: {
      auto slab = reinterpret_cast<Slab<void*>*>(cell);
      int n = (slab->cell_len_ - kSlabHeaderSize) / sizeof(void*);
      for (int i = 0; i < n; ++i) {
        Cell* child = reinterpret_cast<Cell*>(slab->items_[i]);
        if (child) {  // note: List<> may have nullptr; Dict is sparse
          slab->items_[i] = Relocate(child);
        }
      }
      break;
    }

      // other tags like Tag::Opaque have no children
    }
    scan_ += cell->cell_len_;
  }

  log("<-- COLLECT");

  // Swap spaces for next collection
  char* tmp = from_space_;
  from_space_ = to_space_;
  to_space_ = tmp;
}

#if GC_DEBUG
void ShowFixedChildren(Cell* cell) {
  assert(cell->heap_tag == Tag::FixedSize);
  auto fixed = reinterpret_cast<LayoutFixed*>(cell);
  log("MASK:");

  // Note: can this be optimized with the equivalent x & (x-1) trick?
  // We need the index
  // There is a de Brjuin sequence solution?
  // https://stackoverflow.com/questions/757059/position-of-least-significant-bit-that-is-set

  int mask = fixed->field_mask_;
  for (int i = 0; i < 16; ++i) {
    if (mask & (1 << i)) {
      Cell* child = fixed->children_[i];
      // make sure we get Tag::Opaque, Tag::Scanned, etc.
      log("i = %d, p = %p, heap_tag = %d", i, child, child->heap_tag);
    }
  }
}
#endif

}  // namespace gc_heap
