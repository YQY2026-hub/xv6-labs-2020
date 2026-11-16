struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  uint lastuse;     // value of ticks when this buffer was last used (for LRU)
  struct buf *prev; // unused now that the cache is hash-bucketed
  struct buf *next; // next buffer in the same hash bucket
  uchar data[BSIZE];
};

