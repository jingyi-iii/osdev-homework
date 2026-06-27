#include "lib/rbtree.h"
#include "mm/heap.h"

static void left_rotate(rbtree* tree, rbnode* node)
{
	rbnode* right = node->right;
	rbnode* parent = node->parent;

	node->right = right->left;
	if (right->left != tree->nil)
		right->left->parent = node;

	right->left = node;
	right->parent = parent;

	if (parent != tree->nil) {
		if (parent->left == node)
			parent->left = right;
		else
			parent->right = right;
	} else {
		tree->root = right;
	}

	node->parent = right;
}

static void right_rotate(rbtree* tree, rbnode* node)
{
	rbnode* left = node->left;
	rbnode* parent = node->parent;

	node->left = left->right;
	if (left->right != tree->nil)
		left->right->parent = node;

	left->right = node;
	left->parent = parent;

	if (parent != tree->nil) {
		if (parent->left == node)
			parent->left = left;
		else
			parent->right = left;
	} else {
		tree->root = left;
	}

	node->parent = left;
}

static void rbtree_replace_node(rbtree* tree, rbnode* old, rbnode* new)
{
	if (old->parent == tree->nil)
		tree->root = new;
	else if (old->parent->left == old)
		old->parent->left = new;
	else
		old->parent->right = new;

	new->parent = old->parent;
}

rbtree* rbtree_create(void)
{
	rbtree* tree = (rbtree*)kmalloc(sizeof(rbtree));
	if (!tree)
		return 0;

	tree->nil = (rbnode*)kmalloc(sizeof(rbnode));
	if (!tree->nil) {
		kfree(tree);
		return 0;
	}

	tree->nil->col = BLACK;
	tree->nil->left = tree->nil;
	tree->nil->right = tree->nil;
	tree->nil->parent = tree->nil;
	tree->root = tree->nil;

	return tree;
}

rbnode* rbtree_search(rbtree* tree, const void* key, rbtree_key_cmp cmp)
{
	rbnode* node = tree->root;
	int result = 0;

	if (node == tree->nil)
		return 0;

	while (node != tree->nil) {
		result = cmp(key, node);
		if (result < 0) {
			node = node->left;
		} else if (result > 0) {
			node = node->right;
		} else {
			return node;
		}
	}

	return 0;
}

void rbtree_destroy(rbtree* tree)
{
	if (!tree)
		return;

	kfree(tree->nil);
	kfree(tree);
}

static void insert_fixup(rbtree* tree, rbnode* node)
{
	rbnode* uncle = 0;

	while (node->parent != tree->nil && node->parent->col == RED) {
		if (node->parent->parent->left == node->parent) {
			uncle = node->parent->parent->right;
			if (uncle->col == RED) {
				node->parent->col = BLACK;
				uncle->col = BLACK;
				node->parent->parent->col = RED;
				node = node->parent->parent;
				continue;
			}

			// if LR
			if (node == node->parent->right) {
				node = node->parent;
				left_rotate(tree, node);
			}

			// now LL
			node->parent->col = BLACK;
			node->parent->parent->col = RED;
			right_rotate(tree, node->parent->parent);
			break;
		} else {
			uncle = node->parent->parent->left;
			if (uncle->col == RED) {
				node->parent->col = BLACK;
				uncle->col = BLACK;
				node->parent->parent->col = RED;
				node = node->parent->parent;
				continue;
			}

			// if RL
			if (node == node->parent->left) {
				node = node->parent;
				right_rotate(tree, node);
			}

			// now RR
			node->parent->col = BLACK;
			node->parent->parent->col = RED;
			left_rotate(tree, node->parent->parent);
			break;
		}
	}
}

void rbtree_insert(rbtree* tree, rbnode* new_node, rbtree_node_cmp cmp)
{
	rbnode* node = tree->root;
	rbnode* parent = tree->nil;
	int result = 0;

	if (!new_node || !cmp)
		return;

	new_node->col    = RED;
	new_node->left   = tree->nil;
	new_node->right  = tree->nil;
	new_node->parent = tree->nil;

	// BST insert
	if (tree->root == tree->nil) {
		tree->root = new_node;
		new_node->col = BLACK;
		return;
	}

	while (node != tree->nil) {
		parent = node;
		result = cmp(new_node, node);
		if (result < 0) {
			node = node->left;
		} else if (result > 0) {
			node = node->right;
		} else {
			return;
		}
	}

	new_node->parent = parent;
	result = cmp(new_node, parent);
	if (result < 0)
		parent->left = new_node;
	else
		parent->right = new_node;

	insert_fixup(tree, new_node);
	tree->root->col = BLACK;
}

static void delete_fixup(rbtree* tree, rbnode* node)
{
	while (node != tree->root && node->col == BLACK) {
		if (node == node->parent->left) {
			rbnode* brother = node->parent->right;

			// case1, red brother, need to change to case2/3/4
			if (brother->col == RED) {
				brother->col = BLACK;
				brother->parent->col = RED;
				left_rotate(tree, node->parent);
				brother = node->parent->right;
			}

			// case2: black brother is black father
			if (brother->left->col == BLACK && brother->right->col == BLACK) {
				if (brother != tree->nil)
					brother->col = RED;
				node = node->parent;
				continue;
			}
			else {
				// case3: oldest nephew is black, need to change to case4
				if (brother->right->col == BLACK) {
					brother->left->col = BLACK;
					brother->col = RED;
					right_rotate(tree, brother);
					brother = node->parent->right;
				}

				// case4: oldest nephew is red
				brother->col = node->parent->col;
				node->parent->col = BLACK;
				brother->right->col = BLACK;
				left_rotate(tree, node->parent);
				break;
			}
		}
		else {
			rbnode* brother = node->parent->left;

			// case1, red brother, need to change to case2/3/4
			if (brother->col == RED) {
				brother->col = BLACK;
				brother->parent->col = RED;
				right_rotate(tree, node->parent);
				brother = node->parent->left;
			}

			// case2: black brother is black father
			if (brother->left->col == BLACK && brother->right->col == BLACK) {
				if (brother != tree->nil)
					brother->col = RED;
				node = node->parent;
				continue;
			}
			else {
				// case3: youngest nephew is black, need to change to case4
				if (brother->left->col == BLACK) {
					brother->right->col = BLACK;
					brother->col = RED;
					left_rotate(tree, brother);
					brother = node->parent->left;
				}

				// case4: youngest nephew is red
				brother->col = node->parent->col;
				node->parent->col = BLACK;
				brother->left->col = BLACK;
				right_rotate(tree, node->parent);
				break;
			}
		}
	}

	node->col = BLACK;
}

void rbtree_delete(rbtree* tree, rbnode* node)
{
	rbnode* new_node = 0;
	rbnode* fixup_node = 0;
	color removed_color;
	if (!node)
		return;

	removed_color = node->col;

	if (node->left == tree->nil) {
		new_node = node->right;
		fixup_node = new_node;
		rbtree_replace_node(tree, node, node->right);
	} else if (node->right == tree->nil) {
		new_node = node->left;
		fixup_node = new_node;
		rbtree_replace_node(tree, node, node->left);
	} else {
		new_node = node->right;
		while (new_node->left != tree->nil)
			new_node = new_node->left;

		removed_color = new_node->col;
		fixup_node = new_node->right;
		if (new_node->parent == node) {
			node->left->parent = new_node;
			new_node->left = node->left;
			fixup_node->parent = new_node;
		} else {
			// let the new node take control of the original right subtree
			rbtree_replace_node(tree, new_node, new_node->right);
			new_node->right = node->right;
			new_node->right->parent = new_node;
		}

		// let the new node take control of the original left subtree
		rbtree_replace_node(tree, node, new_node);
		new_node->left = node->left;
		new_node->left->parent = new_node;
		new_node->col = node->col;
	}

	if (removed_color == BLACK)
		delete_fixup(tree, fixup_node);
}