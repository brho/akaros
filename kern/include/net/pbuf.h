#ifndef _ROS_PBUF_H_
#define _ROS_PBUF_H_
#include <kmalloc.h>
#include <slab.h>
#include <kref.h>
#include <sys/queue.h>
#include <atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Currently, the pbuf_custom code is only needed for one specific configuration
 * of IP_FRAG */
#define LWIP_SUPPORT_CUSTOM_PBUF (IP_FRAG && !IP_FRAG_USES_STATIC_BUF && !LWIP_NETIF_TX_SINGLE_PBUF)
/* Ensure IP address are 32-bit aligned on 32bit systems, and thus improving the speed of processing
 * for regularly accessed fields such as IP addresses
 */
#define ETH_PAD_SIZE 2      // padding to ensure ip packet is longword aligned.
#define PBUF_TRANSPORT_HLEN 20
#define PBUF_IP_HLEN        20
#define PBUF_LINK_HLEN      14 + ETH_PAD_SIZE // Padding

typedef enum {
  PBUF_TRANSPORT,
  PBUF_IP,
  PBUF_LINK,
  PBUF_RAW
} pbuf_layer;

typedef enum {
  PBUF_RAM, /* pbuf data is stored in RAM */
  PBUF_ROM, /* pbuf data is stored in ROM */
  PBUF_REF, /* pbuf comes from the pbuf pool */
  PBUF_POOL, /* pbuf payload refers to RAM */
	PBUF_MTU  /* pbuf with a fixed MTU size */
} pbuf_type;


/** indicates this is a custom pbuf: pbuf_free and pbuf_header handle such a
    a pbuf differently */
#define PBUF_FLAG_IS_CUSTOM 0x02U
struct pbuf;
STAILQ_HEAD(pbuf_tailq, pbuf);

struct pbuf {
	/* Several reasons to roll own version of STAIL queue here 
	 * pbuf chain exists without a queue
	 * also pbuf chain need to be moved entirely onto a socket queue
	 */
	STAILQ_ENTRY(pbuf) next;
  // struct pbuf *next;

  /** pointer to the actual data in the buffer */
  void *payload;

  uint16_t tot_len;

  /** length of this buffer */
  uint16_t len;

	uint16_t alloc_len;

  /** pbuf_type as u8_t instead of enum to save space */
  uint8_t type;

  /** misc flags */
  uint8_t flags;

  struct kref bufref;
};

struct pbuf_head {
	struct pbuf_tailq pbuf_fifo;
	uint32_t qlen;
	spinlock_t lock; 
};

static inline void pbuf_head_init(struct pbuf_head *ph) {
	STAILQ_INIT(&ph->pbuf_fifo);
	ph->qlen = 0;
	spinlock_init(&ph->lock);
}
extern struct kmem_cache *pbuf_kcache;
/* Initializes the pbuf module. This call is empty for now, but may not be in future. */
void pbuf_init(void);
void pbuf_cat(struct pbuf *head, struct pbuf *tail);
void pbuf_chain(struct pbuf *head, struct pbuf *tail);
void pbuf_ref(struct pbuf *p);
bool pbuf_deref(struct pbuf *p);
int pbuf_header(struct pbuf *p, int header_size);
struct pbuf *pbuf_alloc(pbuf_layer layer, uint16_t length, pbuf_type type);
int pbuf_copy_out(struct pbuf *buf, void *dataptr, size_t len, uint16_t offset);
void print_pbuf(struct pbuf *p);
bool pbuf_free(struct pbuf *p);

void attach_pbuf(struct pbuf *p, struct pbuf_head *buf_head);
struct pbuf* detach_pbuf(struct pbuf_head *buf_head);
// end
#if 0
void pbuf_realloc(struct pbuf *p, u16_t size); 
u8_t pbuf_clen(struct pbuf *p);  
struct pbuf *pbuf_dechain(struct pbuf *p);
err_t pbuf_copy(struct pbuf *p_to, struct pbuf *p_from);
err_t pbuf_take(struct pbuf *buf, const void *dataptr, u16_t len);
struct pbuf *pbuf_coalesce(struct pbuf *p, pbuf_layer layer);
#if LWIP_CHECKSUM_ON_COPY
err_t pbuf_fill_chksum(struct pbuf *p, u16_t start_offset, const void *dataptr,
                       u16_t len, u16_t *chksum);
#endif /* LWIP_CHECKSUM_ON_COPY */

u8_t pbuf_get_at(struct pbuf* p, u16_t offset);
u16_t pbuf_memcmp(struct pbuf* p, u16_t offset, const void* s2, u16_t n);
u16_t pbuf_memfind(struct pbuf* p, const void* mem, u16_t mem_len, u16_t start_offset);
u16_t pbuf_strstr(struct pbuf* p, const char* substr);

#endif

#ifdef __cplusplus
}
#endif

#endif /* __LWIP_PBUF_H__ */
