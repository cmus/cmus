#ifndef PLAYER_DELEGATE_H
#define PLAYER_DELEGATE_H

struct delegate_args_header {
	void *next;
	void (*handler)(void *);
};

void delegate_init(void);
void delegate_exit(void);
void delegate_add_cmd(struct delegate_args_header *header);

#define DELEGATE_FN(fn, ...) \
	struct delegate_args_##fn *args = xmalloc(sizeof(*args)); \
	*args = (struct delegate_args_##fn) { \
		{ NULL, delegate_handler_##fn }, ##__VA_ARGS__ \
	}; \
	delegate_add_cmd(&args->header); \

#define DELEGATE_0(fn) \
	struct delegate_args_##fn { \
		struct delegate_args_header header; \
	}; \
	static void delegate_impl_##fn(void); \
	static void delegate_handler_##fn(void *vargs) \
	{ \
		delegate_impl_##fn(); \
	} \
	void fn(void) \
	{ \
		DELEGATE_FN(fn) \
	} \
	static void delegate_impl_##fn(void)

#define DELEGATE_1(fn, type1, name1) \
	struct delegate_args_##fn { \
		struct delegate_args_header header; \
		type1 a1; \
	}; \
	static void delegate_impl_##fn(type1 name1); \
	static void delegate_handler_##fn(void *vargs) \
	{ \
		struct delegate_args_##fn *args = vargs; \
		delegate_impl_##fn(args->a1); \
	} \
	void fn(type1 a1) \
	{ \
		DELEGATE_FN(fn, a1) \
	} \
	static void delegate_impl_##fn(type1 name1)

#define DELEGATE_2(fn, type1, name1, type2, name2) \
	struct delegate_args_##fn { \
		struct delegate_args_header header; \
		type1 a1; type2 a2; \
	}; \
	static void delegate_impl_##fn(type1 name1, type2 name2); \
	static void delegate_handler_##fn(void *vargs) \
	{ \
		struct delegate_args_##fn *args = vargs; \
		delegate_impl_##fn(args->a1, args->a2); \
	} \
	void fn(type1 a1, type2 a2) \
	{ \
		DELEGATE_FN(fn, a1, a2) \
	} \
	static void delegate_impl_##fn(type1 name1, type2 name2)

#define DELEGATE_3(fn, type1, name1, type2, name2, type3, name3) \
	struct delegate_args_##fn { \
		struct delegate_args_header header; \
		type1 a1; type2 a2; type3 a3; \
	}; \
	static void delegate_impl_##fn(type1 name1, type2 name2, type3 name3); \
	static void delegate_handler_##fn(void *vargs) \
	{ \
		struct delegate_args_##fn *args = vargs; \
		delegate_impl_##fn(args->a1, args->a2, args->a3); \
	} \
	void fn(type1 a1, type2 a2, type3 a3) \
	{ \
		DELEGATE_FN(fn, a1, a2, a3) \
	} \
	static void delegate_impl_##fn(type1 name1, type2 name2, type3 name3)

#endif
