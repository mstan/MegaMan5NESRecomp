#pragma once
void mm5_pool_register(void);
void mm5_pool_refresh(void);
void mm5_pool_reset(void);
void mm5_pool_begin(void);
void mm5_pool_render_begin(void);
void mm5_pool_draw(void);
void mm5_pool_abort(void);
int mm5_pool_busy(void);
void mm5_pool_disable(void);
void mm5_pool_private_begin(void);
void mm5_pool_private_end(int commit);
int mm5_pool_make_room(void);
int mm5_pool_authored(int index);
int mm5_pool_next(unsigned short site);
int mm5_pool_json(char *out,int capacity);
