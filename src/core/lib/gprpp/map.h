#ifndef GRPC_CORE_LIB_GPRPP_MAP_H_
#define GRPC_CORE_LIB_GPRPP_MAP_H_

#include <functional>
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"

// Alternative map implementation for grpc_core
namespace grpc_core {
template <class Key, class Value>
class Map {
 public:
  class Entry {
   public:
    Entry(Key key, Value value)
        : key_(std::move(key)),
          value_(std::move(value)),
          left_(nullptr),
          right_(nullptr),
          height_(1) {}
    Key key() { return std::move(key_); }
    Value value() { return std::move(value_); }
    Entry* left() { return left_; }
    Entry* right() { return right_; }
    long height() { return height_; }

    void set_key(Key k) { key_ = std::move(k); }
    void set_value(Value v) { value_ = std::move(v); }
    void set_left(Entry* left) { left_ = left; }
    void set_right(Entry* right) { right_ = right; }
    void set_height(long height) { height_ = height; }

   private:
    Key key_;
    Value value_;
    Entry* left_;
    Entry* right_;
    long height_;
  };

  class Iterator {
   public:
    Iterator(grpc_core::Map<Key, Value>* map,
             grpc_core::Map<Key, Value>::Entry* curr)
        : curr_(curr), map_(map) {}

    bool operator==(const Iterator& rhs) const { return (curr_ == rhs.curr_); }
    bool operator!=(const Iterator& rhs) const { return (curr_ != rhs.curr_); }

    Iterator& operator++() {
      curr_ = map_->InOrderSuccessor(curr_);
      return *this;
    }

    Iterator& operator++(int e) {
      curr_ = map_->InOrderSuccessor(curr_);
      return *this;
    }

    Value GetValue() { return curr_->value(); }
    Key GetKey() { return curr_->key(); }

    // Removes the current entry and points to the next one
    Value RemoveCurrent() {
      Entry* delEntry = curr_;
      Value ret = std::move(curr_->value());
      curr_ = map_->InOrderSuccessor(curr_);
      map_->Remove(delEntry->key());
      return ret;
    }

   private:
    Entry* curr_;
    Map* map_;
  };

  Map(std::function<int(Key, Key)> comparator)
      : comparator_(comparator), root_(nullptr) {}

  ~Map() { Clear(); }

  long EntryHeight(Entry* e) { return e == nullptr ? 0 : e->height(); }

  void Insert(Key k, Value v) { root_ = InsertRecursive(root_, std::move(k), std::move(v)); }

  Value Find(Key k) {
    Entry* iter = root_;
    while (iter != nullptr) {
      int comp = comparator_(iter->key(), k);
      if (!comp) {
        return iter->value();
      } else if (comp < 0) {
        iter = iter->right();
      } else {
        iter = iter->left();
      }
    }
    return nullptr;
  }

  void Remove(Key k) { root_ = RemoveRecursive(root_, k); }

  bool Empty() { return root_ == nullptr; }

  void Clear() {
    while (!Empty()) {
      Remove(root()->key());
    }
  }

  Entry* root() { return root_; }

  Iterator Begin() {
    Entry* curr = GetMinEntry(root_);
    return Iterator(this, curr);
  }

  Iterator End() { return Iterator(this, nullptr); }

 private:
  Entry* GetMinEntry(Entry* e) {
    if (e != nullptr) {
      while (e->left() != nullptr) {
        e = e->left();
      }
    }
    return e;
  }

  Entry* InOrderSuccessor(Entry* e) {
    if (e->right() != nullptr) {
      return GetMinEntry(e->right());
    }

    Entry* successor = nullptr;
    Entry* iter = root();
    while (iter != nullptr) {
      int comp = comparator_(iter->key(), e->key());
      if (comp > 0) {
        successor = iter;
        iter = iter->left();
      } else if (comp < 0) {
        iter = iter->right();
      } else
        break;
    }
    return successor;
  }

  Entry* RotateLeft(Entry* e) {
    Entry* rightChild = e->right();
    Entry* rightLeftChild = rightChild->left();

    rightChild->set_left(e);
    e->set_right(rightLeftChild);

    e->set_height(1 + GPR_MAX(EntryHeight(e->left()), EntryHeight(e->right())));
    rightChild->set_height(1 + GPR_MAX(EntryHeight(rightChild->left()),
                                       EntryHeight(rightChild->right())));
    return rightChild;
  }

  Entry* RotateRight(Entry* e) {
    Entry* leftChild = e->left();
    Entry* leftRightChild = leftChild->right();

    leftChild->set_right(e);
    e->set_left(leftRightChild);

    e->set_height(1 + GPR_MAX(EntryHeight(e->left()), EntryHeight(e->right())));
    leftChild->set_height(1 + GPR_MAX(EntryHeight(leftChild->left()),
                                      EntryHeight(leftChild->right())));
    return leftChild;
  }

  Entry* RebalanceTreeAfterInsertion(Entry* root, Key k) {
    root->set_height(
        1 + GPR_MAX(EntryHeight(root->left()), EntryHeight(root->right())));
    int heightDifference =
        EntryHeight(root->left()) - EntryHeight(root->right());
    if (heightDifference > 1 && comparator_(root->left()->key(), k) > 0) {
      return RotateRight(root);
    }
    if (heightDifference < -1 && comparator_(root->right()->key(), k) < 0) {
      return RotateLeft(root);
    }
    if (heightDifference > 1 && comparator_(root->left()->key(), k) < 0) {
      root->set_left(RotateLeft(root->left()));
      return RotateRight(root);
    }
    if (heightDifference < -1 && comparator_(root->left()->key(), k) > 0) {
      root->set_right(RotateRight(root->right()));
      return RotateLeft(root);
    }
    return root;
  }

  Entry* RebalanceTreeAfterDeletion(Entry* root) {
    root->set_height(
        1 + GPR_MAX(EntryHeight(root->left()), EntryHeight(root->right())));
    int heightDifference =
        EntryHeight(root->left()) - EntryHeight(root->right());
    if (heightDifference > 1) {
      int leftHeightDifference = EntryHeight(root->left()->left()) -
                                 EntryHeight(root->left()->right());
      if (leftHeightDifference < 0) {
        root->set_left(RotateLeft(root->left()));
      }
      return RotateRight(root);
    }
    if (heightDifference < -1) {
      int rightHeightDifference = EntryHeight(root->right()->left()) -
                                  EntryHeight(root->right()->right());
      if (rightHeightDifference > 0) {
        root->set_right(RotateRight(root->right()));
      }
      return RotateLeft(root);
    }
    return root;
  }

  Entry* InsertRecursive(Entry* root, Key k, Value v) {
    if (root == nullptr) {
      return New<Entry>(std::move(k), std::move(v));
    }

    int comp = comparator_(root->key(), k);
    if (comp > 0) {
      root->set_left(InsertRecursive(root->left(), std::move(k), std::move(v)));
    } else if (comp < 0) {
      root->set_right(InsertRecursive(root->right(), std::move(k), std::move(v)));
    } else {
      root->set_value(std::move(v));
      return root;
    }
    return RebalanceTreeAfterInsertion(root, k);
  }

  Entry* RemoveRecursive(Entry* root, Key k) {
    if (root == nullptr) return root;
    int comp = comparator_(root->key(), k);
    if (comp > 0) {
      root->set_left(RemoveRecursive(root->left(), k));
    } else if (comp < 0) {
      root->set_right(RemoveRecursive(root->right(), k));
    } else {
      Entry* ret;
      if (root->left() == nullptr) {
        ret = root->right();
        grpc_core::Delete(root);
        return ret;
      } else if (root->right() == nullptr) {
        ret = root->left();
        grpc_core::Delete(root);
        return ret;
      } else {
        ret = root->right();
        while (ret->left() != nullptr) {
          ret = ret->left();
        }
        root->set_key(std::move(ret->key()));
        root->set_value(std::move(ret->value()));
        root->set_right(RemoveRecursive(root->right(), ret->key()));
      }
    }
    return RebalanceTreeAfterDeletion(root);
  }
  std::function<int(Key, Key)> comparator_;
  Entry* root_;
};
}  // namespace grpc_core
#endif  // GRPC_CORE_LIB_GPRPP_MAP_H_
