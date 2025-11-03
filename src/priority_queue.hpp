#ifndef SJTU_PRIORITY_QUEUE_HPP
#define SJTU_PRIORITY_QUEUE_HPP

#include <cstddef>
#include <functional>
#include "exceptions.hpp"

namespace sjtu {
/**
 * @brief a container like std::priority_queue which is a heap internal.
 * **Exception Safety**: The `Compare` operation might throw exceptions for certain data.
 * In such cases, any ongoing operation should be terminated, and the priority queue should be restored to its original state before the operation began.
 */
template<typename T, class Compare = std::less<T>>
class priority_queue {
private:
	struct Node {
		T value;
		Node *left;
		Node *right;
		Node(const T &v) : value(v), left(nullptr), right(nullptr) {}
	};

	Node *root = nullptr;
	std::size_t elementCount = 0;
	Compare comp;

	// Guard to rollback a node's children on exceptions during merge
	struct MergeGuard {
		Node *node;
		Node *origLeft;
		Node *origRight;
		bool active;
		explicit MergeGuard(Node *n)
			: node(n), origLeft(n ? n->left : nullptr), origRight(n ? n->right : nullptr), active(true) {}
		void commit() { active = false; }
		~MergeGuard() {
			if (active && node) {
				node->left = origLeft;
				node->right = origRight;
			}
		}
	};

	// Skew heap merge with strong exception safety using MergeGuard
	Node *merge_nodes(Node *a, Node *b) {
		if (a == nullptr) return b;
		if (b == nullptr) return a;
		// Ensure 'a' has the higher priority (i.e., should be on top)
		// If comp(a, b) is true, then a < b, so swap to make 'a' the larger
		if (comp(a->value, b->value)) {
			Node *tmp = a;
			a = b;
			b = tmp;
		}
		MergeGuard guard(a);
		// Recursively merge a->right and b
		Node *newRight = merge_nodes(a->right, b);
		a->right = newRight;
		// Skew heap property: swap children
		Node *tmpChild = a->left;
		a->left = a->right;
		a->right = tmpChild;
		guard.commit();
		return a;
	}

	static Node *clone_nodes(Node *n) {
		if (n == nullptr) return nullptr;
		Node *copy = new Node(n->value);
		try {
			copy->left = clone_nodes(n->left);
			copy->right = clone_nodes(n->right);
		} catch (...) {
			// clean up partially constructed subtree
			clear_nodes(copy);
			throw;
		}
		return copy;
	}

	static void clear_nodes(Node *n) {
		if (!n) return;
		// Use iterative deletion to avoid deep recursion
		// We don't know exact size here; traverse with a manual stack that grows as needed
		// First pass: count size upper bound by simple stackless traversal could be complex;
		// Instead, we'll allocate a dynamic stack that grows by doubling when needed.
		std::size_t cap = 1024;
		Node **stack = new Node*[cap];
		std::size_t top = 0;
		stack[top++] = n;
		while (top > 0) {
			Node *cur = stack[--top];
			if (cur->left) {
				if (top + 1 >= cap) {
					std::size_t newCap = cap * 2;
					Node **newStack = new Node*[newCap];
					for (std::size_t i = 0; i < top; ++i) newStack[i] = stack[i];
					delete[] stack;
					stack = newStack;
					cap = newCap;
				}
				stack[top++] = cur->left;
			}
			if (cur->right) {
				if (top + 1 >= cap) {
					std::size_t newCap = cap * 2;
					Node **newStack = new Node*[newCap];
					for (std::size_t i = 0; i < top; ++i) newStack[i] = stack[i];
					delete[] stack;
					stack = newStack;
					cap = newCap;
				}
				stack[top++] = cur->right;
			}
			delete cur;
		}
		delete[] stack;
	}

public:
	/**
	 * @brief default constructor
	 */
	priority_queue() {}

	/**
	 * @brief copy constructor
	 * @param other the priority_queue to be copied
	 */
	priority_queue(const priority_queue &other) : root(nullptr), elementCount(other.elementCount), comp(other.comp) {
		if (other.root) {
			root = clone_nodes(other.root);
		}
	}

	/**
	 * @brief deconstructor
	 */
	~priority_queue() {
		clear_nodes(root);
		root = nullptr;
		elementCount = 0;
	}

	/**
	 * @brief Assignment operator
	 * @param other the priority_queue to be assigned from
	 * @return a reference to this priority_queue after assignment
	 */
	priority_queue &operator=(const priority_queue &other) {
		if (this == &other) return *this;
		Node *newRoot = nullptr;
		try {
			if (other.root) newRoot = clone_nodes(other.root);
		} catch (...) {
			// cloning failed; leave this unchanged
			throw;
		}
		// success: clear current and assign
		clear_nodes(root);
		root = newRoot;
		elementCount = other.elementCount;
		comp = other.comp;
		return *this;
	}

	/**
	 * @brief get the top element of the priority queue.
	 * @return a reference of the top element.
	 * @throws container_is_empty if empty() returns true
	 */
	const T & top() const {
		if (empty()) throw container_is_empty();
		return root->value;
	}

	/**
	 * @brief push new element to the priority queue.
	 * @param e the element to be pushed
	 */
	void push(const T &e) {
		Node *node = nullptr;
		try {
			node = new Node(e);
			Node *merged = merge_nodes(root, node);
			root = merged;
			elementCount += 1;
		} catch (const runtime_error &) {
			// restore handled by merge guards; delete the newly created node if it wasn't linked
			// If node is already linked, guards restore and we should not double delete; but since guards
			// restore pointers to their originals, 'node' will be detached; safe to delete here
			if (node) {
				// Detach children to avoid recursive delete of existing nodes
				node->left = node->right = nullptr;
				delete node;
			}
			throw; // rethrow same sjtu::runtime_error
		} catch (...) {
			if (node) {
				node->left = node->right = nullptr;
				delete node;
			}
			throw runtime_error();
		}
	}

	/**
	 * @brief delete the top element from the priority queue.
	 * @throws container_is_empty if empty() returns true
	 */
	void pop() {
		if (empty()) throw container_is_empty();
		Node *oldRoot = root;
		Node *left = oldRoot->left;
		Node *right = oldRoot->right;
		try {
			Node *merged = merge_nodes(left, right);
			// commit
			root = merged;
			oldRoot->left = oldRoot->right = nullptr;
			delete oldRoot;
			elementCount -= 1;
		} catch (const runtime_error &) {
			// merge_nodes will rollback any changes in left/right subtrees
			throw;
		} catch (...) {
			throw runtime_error();
		}
	}

	/**
	 * @brief return the number of elements in the priority queue.
	 * @return the number of elements.
	 */
	size_t size() const { return elementCount; }

	/**
	 * @brief check if the container is empty.
	 * @return true if it is empty, false otherwise.
	 */
	bool empty() const { return elementCount == 0; }

	/**
	 * @brief merge another priority_queue into this one.
	 * The other priority_queue will be cleared after merging.
	 * The complexity is at most O(logn).
	 * @param other the priority_queue to be merged.
	 */
	void merge(priority_queue &other) {
		if (this == &other) return; // no-op on self-merge
		try {
			Node *merged = merge_nodes(root, other.root);
			root = merged;
			elementCount += other.elementCount;
			other.root = nullptr;
			other.elementCount = 0;
		} catch (const runtime_error &) {
			// rollback handled by guards inside merge_nodes
			throw;
		} catch (...) {
			throw runtime_error();
		}
	}
};

}

#endif