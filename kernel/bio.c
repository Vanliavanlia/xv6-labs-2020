// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13
#define HASH(id) (id % NBUCKET)

struct bucket {
  struct spinlock lock;
  struct buf head; // 每个桶里自己的双向链表头
};

struct {
  struct buf buf[NBUF];
  struct bucket buckets[NBUCKET]; // 13个哈希桶
} bcache;

void
binit(void)
{
  struct buf *b;
  char name[16];

  for (int i = 0; i < NBUCKET; i++) {
    snprintf(name, sizeof(name), "bcache_%d", i);
    initlock(&bcache.buckets[i].lock, name);
    // 初始化桶内的链表头，自己指向自己
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
  }

  // 初始化所有的缓冲区，全部挂在 0 号桶
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    initsleeplock(&b->lock, "buffer");
    b->last_use = 0;
    b->next = bcache.buckets[0].head.next;
    b->prev = &bcache.buckets[0].head;
    bcache.buckets[0].head.next->prev = b;
    bcache.buckets[0].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int id = HASH(blockno);

  acquire(&bcache.buckets[id].lock);

  // 1. 在自己桶里找有没有
  for(b = bcache.buckets[id].head.next; b != &bcache.buckets[id].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      b->last_use = ticks;
      release(&bcache.buckets[id].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 2. 自己桶里找空闲的（LRU）
  struct buf *victim = 0;
  uint min_time = 0xffffffff;
  for(b = bcache.buckets[id].head.next; b != &bcache.buckets[id].head; b = b->next){
    if(b->refcnt == 0 && b->last_use < min_time){
      victim = b;
      min_time = b->last_use;
    }
  }

  // 3. 如果自己桶里没空闲的，必须去别的桶偷
  if(victim == 0) {
    // 👇 关键：先去偷，偷到了再拿自己的锁。释放自己的锁，避免死锁！
    release(&bcache.buckets[id].lock);

    for (int i = 0; i < NBUCKET; i++) {
      if (i == id) continue;
      acquire(&bcache.buckets[i].lock);
      
      for(b = bcache.buckets[i].head.next; b != &bcache.buckets[i].head; b = b->next){
        if(b->refcnt == 0 && b->last_use < min_time){
          victim = b;
          min_time = b->last_use;
        }
      }
      
      if (victim) {
        // 从别人的链表里拔出来
        victim->prev->next = victim->next;
        victim->next->prev = victim->prev;
        release(&bcache.buckets[i].lock); // 偷到了，释放别人的锁
        break;
      }
      release(&bcache.buckets[i].lock); // 没偷到，释放别人的锁继续找
    }

    // 偷完后，重新拿回自己桶的锁
    acquire(&bcache.buckets[id].lock);

    // 如果偷到了，把 victim 插回自己的桶
    if (victim) {
      victim->next = bcache.buckets[id].head.next;
      victim->prev = &bcache.buckets[id].head;
      bcache.buckets[id].head.next->prev = victim;
      bcache.buckets[id].head.next = victim;
    }
  }

  // 4. 找到了替死鬼，更新它的信息
  if (victim) {
    victim->dev = dev;
    victim->blockno = blockno;
    victim->valid = 0;
    victim->refcnt = 1;
    victim->last_use = ticks;
    release(&bcache.buckets[id].lock);
    acquiresleep(&victim->lock);
    return victim;
  }

  // 如果所有桶都满了，说明系统资源耗尽
  release(&bcache.buckets[id].lock);
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int id = HASH(b->blockno);
  acquire(&bcache.buckets[id].lock);
  b->refcnt--;
  b->last_use = ticks; // 更新时间
  release(&bcache.buckets[id].lock);
}

void
bpin(struct buf *b) {
  int id = HASH(b->blockno);
  acquire(&bcache.buckets[id].lock);
  b->refcnt++;
  release(&bcache.buckets[id].lock);
}

void
bunpin(struct buf *b) {
  int id = HASH(b->blockno);
  acquire(&bcache.buckets[id].lock);
  b->refcnt--;
  release(&bcache.buckets[id].lock);
}

