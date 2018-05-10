#define FUSE_USE_VERSION 26
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>
#define BLOCKSIZE (64 * 1024)
#define SIZE ((size_t)4 * 1024 * 1024 * 1024)
#define BLOCKNR ((SIZE) / (BLOCKSIZE))
#define HEAD (sizeof(struct filenode))
#define min(a, b) (((a) < (b)) ? (a) : (b))


typedef struct filenode {
	char *filename;
	int bnum;
	struct stat *st;
	struct filenode *next;
}fnode;
typedef struct block_head {
	int usedsize;
	int nextblock;
}bhead;

static const size_t size = SIZE;
static void *mem[BLOCKNR];
static const size_t blocksize = BLOCKSIZE;
static const size_t blocknr = BLOCKNR;
static struct filenode *root = NULL;

static int last_used_block = 0;
struct information {
	int blockused;
	size_t size;
	int filenum;
	struct filenode *root;
};
struct information *info;

int init_block(int blocknum)
{
	bhead *head;
	if (mem[blocknum])
		return -1;
	info->blockused++;
	mem[blocknum] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	memset(mem[blocknum], 0, blocksize);
	head = (bhead*)mem[blocknum];
	head->usedsize = sizeof(bhead);
	head->nextblock = -1;
	last_used_block = blocknum;
	return 0;
}
/*find next available block*/
int find_avail_block(void)
{
	int i;
	for (i = (last_used_block + 1) % (int)blocknr; i != last_used_block; i = (i + 1) % (int)blocknr)
		if (!mem[i])
			return i;
	return -1;
}
/*get memory of size in the given block*/
static void *getmem(int blocknum, int size)
{
	if (!mem[blocknum])
		return NULL;
	bhead *head = (bhead*)mem[blocknum];
	if (size > BLOCKSIZE - head->usedsize)
		size = size - head->usedsize;
	head->usedsize += size;
	return mem[blocknum] + head->usedsize - size;
}
/*get filenode by the filename*/
static struct filenode *get_filenode(const char *name)
{
	struct filenode *node = root;
	while (node) {
		if (strcmp(node->filename, name + 1) != 0)
			node = node->next;
		else
			return node;
	}
	return NULL;
}
/*find and init next available block*/
int setnext(int blocknum)
{
	bhead *head;
	int next = find_avail_block();
	if (next == -1)
		return -1;
	init_block(next);
	head = (bhead*)mem[blocknum];
	head->nextblock = next;
	return next;
}
/*find a new block to create filenode*/
static void create_filenode(const char *filename, const struct stat *st)
{
	int blocknum;
	bhead *head;
	blocknum = find_avail_block();
	init_block(blocknum);
	info->filenum++;
	head = (bhead*)mem[blocknum];
	struct filenode *new = (struct filenode *)getmem(blocknum, sizeof(struct filenode));
	new->filename = (char *)getmem(blocknum, strlen(filename) + 1);
	memcpy(new->filename, filename, strlen(filename) + 1);
	new->st = (struct stat *)getmem(blocknum, sizeof(struct stat));
	memcpy(new->st, st, sizeof(struct stat));
	new->next = root;
	new->bnum = blocknum;
	root = new;
	info->root = new;
}

static void *oshfs_init(struct fuse_conn_info *conn)
{
	mem[0] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	info = (struct information*)mem[0];
	info->blockused = 1;
	info->filenum = 0;
	info->size = 0;
	info->root = NULL;
	return NULL;
}

static int oshfs_getattr(const char *path, struct stat *stbuf)
{
	int ret = 0;
	struct filenode *node = get_filenode(path);
	if (strcmp(path, "/") == 0) {
		memset(stbuf, 0, sizeof(struct stat));
		stbuf->st_mode = S_IFDIR | 0755;
	}
	else if (node) {
		memcpy(stbuf, node->st, sizeof(struct stat));
	}
	else {
		ret = -ENOENT;
	}
	return ret;
}

static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	struct filenode *node = root;
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	while (node) {
		filler(buf, node->filename, node->st, 0);
		node = node->next;
	}
	return 0;
}

static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)
{
	struct stat st;
	st.st_mode = S_IFREG | 0644;
	st.st_uid = fuse_get_context()->uid;
	st.st_gid = fuse_get_context()->gid;
	st.st_nlink = 1;
	st.st_size = 0;
	st.st_ctime = st.st_mtime = st.st_atime = time(NULL);
	create_filenode(path + 1, &st);
	return 0;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi)
{
	return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	bhead *head;
	size_t done;
	struct filenode *node = get_filenode(path);
	info->size += size + offset - node->st->st_size;
	node->st->st_size = offset + size;
	node->st->st_mtime = time(NULL);
	int blocknum;
	blocknum = node->bnum;
	head = (bhead*)mem[blocknum];
	/*find start address*/
	while (offset > BLOCKSIZE - head->usedsize) {
		offset -= BLOCKSIZE - head->usedsize;
		if (head->nextblock == -1)
			blocknum = setnext(blocknum);
		else
			blocknum = head->nextblock;
		head = (bhead*)mem[blocknum];
	}
	memcpy(mem[blocknum] + head->usedsize + offset, buf, min(size, BLOCKSIZE - head->usedsize - offset));
	done = min(size, BLOCKSIZE - head->usedsize - offset);
	while (done < size) {
		if (head->nextblock == -1)
			blocknum = setnext(blocknum);
		else
			blocknum = head->nextblock;
		head = (bhead*)mem[blocknum];
		memcpy(mem[blocknum] + head->usedsize, buf + done, min(size - done, BLOCKSIZE - head->usedsize));
		done += min(size - done, BLOCKSIZE - head->usedsize);
	}
	return size;
}

static int oshfs_truncate(const char *path, off_t size)
{
	struct filenode *node = get_filenode(path);
	info->size += size - node->st->st_size;
	int blocknum = node->bnum;
	node->st->st_mtime = time(NULL);
	bhead *head = (bhead*)mem[blocknum];
	node->st->st_size = size;
	while (size > BLOCKSIZE - head->usedsize) {
		if (head->nextblock == -1)
			blocknum = setnext(blocknum);
		else
			blocknum = head->nextblock;
		size -= BLOCKSIZE - head->usedsize;
		head = (bhead*)mem[blocknum];
	}
	return 0;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct filenode *node = get_filenode(path);
	node->st->st_atime = time(NULL);
	int ret = size;
	size_t done;
	int blocknum = node->bnum;
	bhead *head = (bhead*)mem[blocknum];
	if (offset + size > node->st->st_size)
		ret = node->st->st_size - offset;
	/*find start address*/
	while (offset > BLOCKSIZE - head->usedsize) {
		offset -= BLOCKSIZE - head->usedsize;
		if (head->nextblock == -1)
			return -1;
		else
			blocknum = head->nextblock;
		head = (bhead*)mem[blocknum];
	}
	memcpy(buf, mem[blocknum] + head->usedsize + offset, min(ret, BLOCKSIZE - head->usedsize - offset));
	done = min(ret, BLOCKSIZE - head->usedsize - offset);
	while (done < ret) {
		if (head->nextblock == -1)
			return -1;
		else
			blocknum = head->nextblock;
		head = (bhead*)mem[blocknum];
		memcpy(buf + done, mem[blocknum] + head->usedsize, min(ret - done, BLOCKSIZE - head->usedsize));
		done += min(ret - done, BLOCKSIZE - head->usedsize);
	}
	return ret;
}

static int oshfs_unlink(const char *path)
{
	struct filenode *node = root;
	int blocknum = -1;
	if (strcmp(root->filename, path + 1) == 0) {
		blocknum = root->bnum;
		root = root->next;
		info->root = root;
	}
	else {
		while (node->next) {
			if (strcmp(node->next->filename, path + 1) != 0)
				node = node->next;
			else {
				blocknum = node->next->bnum;
				node->next = node->next->next;
				break;
			}
		}
	}
	if (blocknum == -1)
		return -1;
	bhead* head = (bhead*)mem[blocknum];
	int next = head->nextblock;
	while (next != -1) {
		info->size -= blocksize - head->usedsize;
		munmap(mem[blocknum], BLOCKSIZE);
		mem[blocknum] = NULL;
		info->blockused--;
		head = (bhead*)mem[next];
		next = head->nextblock;
	}
	info->filenum--;
	return 0;
}
static int oshfs_chmod(const char *path, mode_t mode) {
	struct filenode *node = get_filenode(path);
	node->st->st_mode = mode;
	node->st->st_ctime = time(NULL);
	return 0;
}
static int oshfs_chown(const char *path, uid_t uid, gid_t gid) {
	struct filenode *node = get_filenode(path);
	node->st->st_uid = uid;
	node->st->st_gid = gid;
	node->st->st_ctime = time(NULL);
	return 0;
}

static const struct fuse_operations op = {
	.init = oshfs_init,
	.getattr = oshfs_getattr,
	.readdir = oshfs_readdir,
	.mknod = oshfs_mknod,
	.open = oshfs_open,
	.write = oshfs_write,
	.truncate = oshfs_truncate,
	.read = oshfs_read,
	.unlink = oshfs_unlink,
	.chmod = oshfs_chmod,
	.chown = oshfs_chown
};

int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &op, NULL);
}