#define FUSE_USE_VERSION 26
#define test 0
#if test
#include <stdio.h>
#endif
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>
#include <stdlib.h>
#define BLOCKSIZE (64 * 1024)
#define SIZE ((size_t)4 * 1024 * 1024 * 1024)
#define BLOCKNR ((SIZE) / (BLOCKSIZE))
#define HEAD (sizeof(struct filenode))
#define min(a, b) (((a) < (b)) ? (a) : (b))

typedef unsigned short int16;
typedef struct filenode {
	char filename[128];
	/*for file it's the num of the first block, for dir it points to first file/dir belong to the dir*/
	union fst {
		int16 bhead; //file
		struct filenode* pdir; //dir
	}first;
	/*for file it's the num of the last block, for dir it points to its father dir*/
	union lst {
		int16 lastblock; //file
		struct filenode *father; //dir
	}last;
	struct stat st;
	struct filenode *next;
}fnode;


static const size_t size = SIZE;
static void *mem[BLOCKNR];
static const size_t blocksize = BLOCKSIZE;
static const size_t blocknr = BLOCKNR;
static struct filenode *root = NULL;
int maxfile;
/*
use static linklist to contain block info under the following rules:
if (block is free) contains the num of the next free block or 0 if it's the last free block;
else  contains the num of the next used block which belong to the same file or 0 if it's the last one;
blocklist[0] contains the first free block;
*/
int16 *blocklist;
fnode* freefile;
fnode *newly_used_file;
char newly_used_path[512];


int16 init_block(void)
{
	int16 blocknum = blocklist[0];
#if test
	printf("init block %d\n", blocknum);
#endif
	if (mem[blocknum])
		return 1;
	mem[blocknum] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	memset(mem[blocknum], 0, blocksize);
	blocklist[0] = blocklist[blocknum];
	blocklist[blocknum] = 0;
	return blocknum;
}
/*get father by the path*/
static struct filenode *get_father(const char *path)
{
#if test
	printf("finding father:%s\n",path);
	printf("first free block:%u\n", blocklist[0]);
#endif
	struct filenode *node = root;
	char *name;
	char *temp1, *temp2;
	if (strcmp(path, "/") == 0)
		return root;
	name = (char*)malloc(sizeof(char)*(strlen(path) + 1));
	memcpy(name, path, sizeof(char)*(strlen(path) + 1));
	for (temp1 = name, temp2 = temp1; temp2 != '\0'; temp1 = temp2) {
		temp1++;
		for (temp2 = temp1 + 1; *temp2 != '/'&&*temp2 != '\0'; temp2++)
			;
		if (*temp2 == '\0') {
			free(name);
			return node;
		}
		if (*temp2 == '/') {
			*temp2 = '\0';
			node = node->first.pdir;
#if test
			printf("&&&%s&&&\n", node->filename);
#endif
			for (; node != NULL; node = node->next)
				if (strcmp(temp1, node->filename) == 0)
					break;
			if (node == NULL)
				return NULL;
		}
	}
	return NULL;
}
/*find a new block to create filenode*/
int create_filenode(const char *path, const struct stat *st)
{
#if test
	printf("im creating filenode:%s\n",path);
#endif
	fnode *node, *father;
	char *temp1, *temp2, *name;
	node = freefile;
	if (node == NULL)
		return -ENOSPC;
	freefile = freefile->next;
	father = get_father(path);
	if (father == NULL)
		return -ENOENT;
	name = (char*)malloc(sizeof(char)*(strlen(path) + 1));
	memcpy(name, path, sizeof(char)*(strlen(path) + 1));
	for (temp1 = name, temp2 = temp1; temp2 != '\0'; temp1 = temp2) {
		temp1++;
		for (temp2 = temp1 + 1; *temp2 != '/'&&*temp2 != '\0'; temp2++)
			;
		if (*temp2 == '\0')
			break;
		}
	memcpy(node->filename, temp1, strlen(temp1));
	if (st->st_mode&S_IFDIR == S_IFDIR) {
#if test
		printf("creating a dir\n");
#endif
		node->first.pdir = NULL;
		node->last.father = father;
	}
	else {
#if test
		printf("creating a file\n");
#endif
		node->first.bhead = 0;
		node->last.lastblock = 0;
	}
	memcpy(&node->st, st, sizeof(struct stat));
	node->next = father->first.pdir;
	father->first.pdir = node;
	free(name);
#if test
	printf("done creating filenode:%s\n", root->first.pdir->filename);
#endif
	return 0;
}
/*get filenode by the path*/
static struct filenode *get_filenode(const char *path)
{
#if test
	printf("get filenode:%s\n", path);
#endif
	if (strcmp(path, newly_used_path) == 0 && *newly_used_file->filename != '\0') {
#if test
		printf("HIT!!!!!\n");
#endif
		return newly_used_file;
	}
	struct filenode *node;
	if (strcmp(path, "/") == 0)
		return root;
	else {
		node = get_father(path);
#if test
		printf("getfatherdone:%s\n", node->filename);
#endif
		node = node->first.pdir;
	}
	char *temp1, *temp2, *name;
	name = malloc(sizeof(char)*(strlen(path) + 1));
	memcpy(name, path, sizeof(char)*(strlen(path) + 1));
	for (temp1 = name, temp2 = temp1; temp2 != '\0'; temp1 = temp2) {
		temp1++;
		for (temp2 = temp1 + 1; *temp2 != '/'&&*temp2 != '\0'; temp2++)
			;
		if (*temp2 == '\0')
			break;
	}
	while (node) {
#if test
		printf("%s:%s----------------------\n", node->filename, temp1);
#endif
		if (strcmp(node->filename, temp1) != 0)
			node = node->next;
		else {
			free(name);
			strcpy(newly_used_path, path);
			newly_used_file = node;
			return node;
		}
	}
#if test
	printf("aaaaaaaaaaaaaaaaaaaaaa\n");
#endif
	free(name);
	return NULL;
}
/*free block by blocknum*/
void free_block(int16 blocknum) {
#if test
	printf("free block %u\n", blocknum);
#endif
	int16 temp = blocklist[0];
	blocklist[0] = blocknum;
	munmap(mem[blocknum], blocksize);
	mem[blocknum] = NULL;
	while (blocklist[blocknum] != 0) {
		blocknum = blocklist[blocknum];
		munmap(mem[blocknum], blocksize);
#if test
		printf("free block %u\n", blocknum);
#endif
		mem[blocknum] = NULL;
	}
	blocklist[blocknum] = temp;
}
int rmfile(fnode *father, char *name) {
	fnode *node, *temp;
	node = father;
	if (node == NULL)
		return -ENOENT;
	if (strcmp(node->first.pdir->filename, name) == 0) {
		temp = node->first.pdir;
		node->first.pdir = temp->next;
	}
	else {
		node = node->first.pdir;
		while (node->next != NULL) {
			if (strcmp(node->next->filename, name) != 0)
				node = node->next;
			else {
				temp = node->next;
				node->next = temp->next;
				break;
			}
		}
	}
	if (temp == NULL)
		return -1;
	free_block(temp->first.bhead);
	memset(temp, 0, sizeof(fnode));
	temp->next = freefile;
	freefile = temp;
	return 0;
}
/*rm all files and dirs in a dir*/
int rmdir(fnode *father, char *name) {
#if test
	printf("start rmdir:%s\n", name);
#endif
	fnode *node, *temp;
	node = father;
	if (node == NULL)
		return -ENOENT;
	if (strcmp(node->first.pdir->filename, name) == 0) {
		temp = node->first.pdir;
		node->first.pdir = temp->next;
	}
	else {
		node = node->first.pdir;
		while (node->next != NULL) {
			if (strcmp(node->next->filename, name) != 0)
				node = node->next;
			else {
				temp = node->next;
				node->next = temp->next;
				break;
			}
		}
	}
	if (temp == NULL)
		return -1;
	memset(temp, 0, sizeof(fnode));
	temp->next = freefile;
	freefile = temp;
	return 0;
}

static void *oshfs_init(struct fuse_conn_info *conn)
{
#if test
	printf("init\n");
#endif
	/*use mem[29:0] to contain file info*/
	mem[0] = mmap(NULL, blocksize * 30, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	maxfile = 30 * blocksize / sizeof(fnode);
	int i;
	/*make mem[29:1] not NULL*/
	for (i = 1; i < 30; i++)
		mem[i] = mem[0];
	freefile = (fnode*)mem[0];
	for (i = 0; i < maxfile - 1; i++)
		freefile[i].next = freefile + i + 1;
	freefile[maxfile - 1].next = NULL;
	/*use mem[31:30] to contain block info*/
	mem[30] = mmap(NULL, blocksize * sizeof(int16), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	mem[31] = mem[30];
	blocklist = (int16*)mem[30];
	blocklist[0] = 32;
	for (i = 1; i < blocksize - 1; i++)
		blocklist[i] = i + 1;
	blocklist[29] = 0;
	blocklist[31] = 0;
	blocklist[blocknr] = 0;
	/*init dir "/"*/
	root = freefile;
	freefile = freefile->next;
	memcpy(root->filename, "/", sizeof("/"));
	root->first.pdir = NULL;
	root->last.father = root;
	root->next = NULL;
	root->st.st_mode = 0755 | S_IFDIR;
	root->st.st_atime = root->st.st_ctime = root->st.st_mtime = time(NULL);
	root->st.st_uid = fuse_get_context()->uid;
	root->st.st_gid = fuse_get_context()->gid;
	root->st.st_nlink = 1;
	root->st.st_size = sizeof(fnode);
	newly_used_file = root;
	strcpy(newly_used_path, "/");
	return NULL;
}
static int oshfs_getattr(const char *path, struct stat *stbuf)
{
#if test
	printf("getattr:%s\n", path);
#endif
	int ret = 0;
	struct filenode *node = get_filenode(path);
	if (node == NULL)
		return -ENOENT;
	if (node) {
		memcpy(stbuf, &node->st, sizeof(struct stat));
	}
	else {
		ret = -ENOENT;
	}
	return ret;
}
static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
#if test
	printf("readdir:%s\n", path);
#endif
	struct filenode *node;
	node = get_filenode(path);
	if (node == NULL)
		return -ENOENT;
	if (node->st.st_mode&S_IFDIR != S_IFDIR)
		return -ENOTDIR;
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	node = node->first.pdir;
	while (node) {
		filler(buf, node->filename, &node->st, 0);
		node = node->next;
	}
	return 0;
}
static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)
{
#if test
	printf("mknod:%s\n", path);
#endif
	int ret;
	struct stat st;
	st.st_mode = mode | S_IFREG;
	st.st_uid = fuse_get_context()->uid;
	st.st_gid = fuse_get_context()->gid;
	st.st_nlink = 1;
	st.st_size = 0;
	st.st_ctime = st.st_mtime = st.st_atime = time(NULL);
	ret = create_filenode(path, &st);
	return ret;
}
static int oshfs_mkdir(const char *path, mode_t mode)
{
#if test
	printf("mkdir:%s\n", path);
#endif
	struct stat st;
	int ret;
	st.st_mode = S_IFDIR | mode;
	st.st_uid = fuse_get_context()->uid;
	st.st_gid = fuse_get_context()->gid;
	st.st_nlink = 1;
	st.st_size = sizeof(fnode);
	st.st_ctime = st.st_mtime = st.st_atime = time(NULL);
	ret = create_filenode(path, &st);
	return ret;
}
static int oshfs_open(const char *path, struct fuse_file_info *fi)
{
#if test
	printf("open %s\n", path);
#endif
	fnode *node;
	node = get_filenode(path);
	if (node == NULL)
		return -ENOENT;
	return 0;
}
static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
#if test
	printf("write:%s\n", path);
#endif
	size_t ret;
	int16 blocknum;
	fnode *node;
	int temp;
	node = get_filenode(path);
	if (node == NULL)
		return ENOENT;
	ret = node->st.st_size;
	if (offset + size > node->st.st_size)
		node->st.st_size = offset + size;
	node->st.st_mtime = time(NULL);
	/*find start address*/
#if test
	printf("offset  %ld,  comp  %ld\n", offset, ((ret + blocksize - 1) / blocksize - 1)*blocksize);
#endif
	if (offset >= ((ret + blocksize - 1) / blocksize - 1)*blocksize) {
#if test
		printf("use the last block!!\n");
#endif
		blocknum = node->last.lastblock;
		offset -= ((ret + blocksize - 1) / blocksize - 1)*blocksize;
	}
	else {
		blocknum = node->first.bhead;
		if (blocknum == 0) {
			blocknum = init_block();
			if (blocknum == 1)
				return -ENOSPC;
			node->first.bhead = node->last.lastblock = blocknum;
		}
	}
	ret = 0;
#if test
	printf("1  write start with block %u---offset %ld\n", blocknum, offset);
#endif
	while (offset >= blocksize) {
		if (blocklist[blocknum] == 0) {
			blocklist[blocknum] = init_block();
			if (blocknum == 1)
				return -ENOSPC;
			node->last.lastblock = blocklist[blocknum];
		}
		blocknum = blocklist[blocknum];
		offset -= blocksize;
	}
	ret = min(blocksize - offset, size);
	memcpy(mem[blocknum] + offset, buf, ret);
	while (ret < size) {
		if (blocklist[blocknum] == 0) {
			blocklist[blocknum] = init_block();
			if (blocknum == 1)
				return -ENOSPC;
			node->last.lastblock = blocklist[blocknum];
		}
		blocknum = blocklist[blocknum];
		if (blocknum == 0)
			return ret;
#if test
		printf("2  write start with block %u---offset %ld\n", blocknum, offset);
#endif
		memcpy(mem[blocknum], buf + ret, min(blocksize, size - ret));
		ret += min(blocksize, size - ret);
	}
#if test
	printf("done: %ld\n", ret);
#endif
	return ret;
}
static int oshfs_truncate(const char *path, off_t size)
{
#if test
	printf("truncate:%s -> %ld\n", path, size);
#endif
	struct filenode *node = get_filenode(path);
	if (node == NULL)
		return -ENOENT;
	node->st.st_size = size;
	int16 blocknum = node->first.bhead;
	while (size >= blocksize) {
		if (blocklist[blocknum] == 0) {
			blocklist[blocknum] = init_block();
			if (blocknum == 1)
				return -ENOSPC;
			node->last.lastblock = blocklist[blocknum];
		}
		blocknum = blocklist[blocknum];
		size -= blocksize;
	}
	if (blocklist[blocknum] != 0) {
		free_block(blocklist[blocknum]);
		node->last.lastblock = blocknum;
	}
	blocklist[blocknum] = 0;
	return 0;
}
static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
#if test
	printf("read:%s\n", path);
#endif
	struct filenode *node = get_filenode(path);
	if (node == NULL)
		return -ENOENT;
	node->st.st_atime = time(NULL);
	int ret = size;
	size_t done;
	int16 blocknum = node->first.bhead;
	if (offset + size > node->st.st_size)
		ret = node->st.st_size - offset;
	/*find start address*/
	while (offset > blocksize) {
		offset -= blocksize;
		if (blocklist[blocknum] == 0)
			return -1;
		else
			blocknum = blocklist[blocknum];
	}
#if test
	printf("read block %u\n", blocknum);
#endif
	memcpy(buf, mem[blocknum] + offset, min(ret, blocksize - offset));
	done = min(ret, blocksize - offset);
	while (done < ret) {
		if (blocklist[blocknum] == 0)
			return -1;
		else
			blocknum = blocklist[blocknum];
#if test
		printf("read block %u\n", blocknum);
#endif
		memcpy(buf + done, mem[blocknum], min(ret - done, blocksize));
		done += min(ret - done, blocksize);
	}
	return ret;
}
static int oshfs_unlink(const char *path)
{
#if test
	printf("unlink:%s\n", path);
#endif
	fnode *father = get_father(path);
	if(father==NULL)
		return -ENOENT;
	char  *temp1, *temp2, *name;
	name = (char*)malloc(sizeof(char)*(strlen(path) + 1));
	memcpy(name, path, sizeof(char)*(strlen(path) + 1));
	for (temp1 = name, temp2 = temp1; temp2 != '\0'; temp1 = temp2) {
		temp1++;
		for (temp2 = temp1 + 1; *temp2 != '/'&&*temp2 != '\0'; temp2++)
			;
		if (*temp2 == '\0')
			break;
	}
	int ret = rmfile(father, temp1);
	free(name);
	return ret;
}
static int oshfs_rmdir(const char *path)
{
#if test
	printf("rmdir:%s\n", path);
#endif
	fnode  *father;
	father = get_father(path);
	if (father == NULL)
		return -ENOENT;
	char  *temp1, *temp2, *name;
	name = (char*)malloc(sizeof(char)*(strlen(path) + 1));
	memcpy(name, path, sizeof(char)*(strlen(path) + 1));
	for (temp1 = name, temp2 = temp1; temp2 != '\0'; temp1 = temp2) {
		temp1++;
		for (temp2 = temp1 + 1; *temp2 != '/'&&*temp2 != '\0'; temp2++)
			;
		if (*temp2 == '\0')
			break;
	}
#if test
	printf("---------------rmdir %s,%s\n", father->filename, temp1);
#endif
	int ret = rmdir(father, temp1);
	free(name);
	return ret;
}
static int oshfs_rename(const char *old_p, const char *new_p)
{
#if test
	printf("rename:%s -> %s\n", old_p, new_p);
#endif
	fnode *father, *node;
	fnode *temp;
	father = get_father(old_p);
	node = father;
	if (node == NULL)
		return -ENOENT;
	char  *temp1, *temp2, *name;
	name = (char*)malloc(sizeof(char)*(strlen(old_p) + 1));
	memcpy(name, old_p, sizeof(char)*(strlen(old_p) + 1));
	for (temp1 = name, temp2 = temp1; temp2 != '\0'; temp1 = temp2) {
		temp1++;
		for (temp2 = temp1 + 1; *temp2 != '/'&&*temp2 != '\0'; temp2++)
			;
		if (*temp2 == '\0')
			break;
	}
	if (strcmp(node->first.pdir->filename, temp1) == 0) {
		temp = node->first.pdir;
		node->first.pdir = temp->next;
	}
	else {
		node = node->first.pdir;
		while (node->next != NULL) {
			if (strcmp(node->next->filename, temp1) != 0)
				node = node->next;
			else {
				temp = node->next;
				node->next = temp->next;
				break;
			}
		}
	}
	if (temp == NULL)
		return -ENOENT;
	free(name);
	node = get_filenode(new_p);
	if (node != NULL)
		return -EEXIST;
#if test
	printf("find old file:%s\n", temp->filename);
#endif
	name = (char*)malloc(sizeof(char)*(strlen(new_p) + 1));
	memcpy(name, new_p, sizeof(char)*(strlen(new_p) + 1));
	for (temp1 = name, temp2 = temp1; temp2 != '\0'; temp1 = temp2) {
		temp1++;
		for (temp2 = temp1 + 1; *temp2 != '/'&&*temp2 != '\0'; temp2++)
			;
		if (*temp2 == '\0')
			break;
	}
	memcpy(temp->filename, temp1, sizeof(char)*(strlen(temp1) + 1));
#if test
	printf("change to new file:%s\n", temp->filename);
#endif
	father = get_father(new_p);
	temp->next = father->first.pdir;
	father->first.pdir = temp;
	free(name);
	return 0;
}
static int oshfs_chmod(const char *path, mode_t mode) 
{
	struct filenode *node = get_filenode(path);
	if (node == NULL)
		return -ENOENT;
	node->st.st_mode = mode;
	node->st.st_ctime = time(NULL);
	return 0;
}
static int oshfs_chown(const char *path, uid_t uid, gid_t gid) 
{
	struct filenode *node = get_filenode(path);
	if (node == NULL)
		return -ENOENT;
	node->st.st_uid = uid;
	node->st.st_gid = gid;
	node->st.st_ctime = time(NULL);
	return 0;
}
static int oshfs_utimens(const char *path, const struct timespec tv[2])
{
	struct filenode *node = get_filenode(path);
	if (node == NULL)
		return -ENOENT;
	node->st.st_atim.tv_nsec = tv[0].tv_nsec;
	node->st.st_atim.tv_sec = tv[0].tv_sec;
	node->st.st_mtim.tv_nsec = tv[1].tv_nsec;
	node->st.st_mtim.tv_sec = tv[1].tv_sec;
	return 0;
}


static const struct fuse_operations op = {
	.init = oshfs_init,
	.getattr = oshfs_getattr,
	.readdir = oshfs_readdir,
	.mknod = oshfs_mknod,
	.mkdir = oshfs_mkdir,
	.open = oshfs_open,
	.write = oshfs_write,
	.truncate = oshfs_truncate,
	.read = oshfs_read,
	.unlink = oshfs_unlink,
	.rmdir = oshfs_rmdir,
	.rename = oshfs_rename,
	.chmod = oshfs_chmod,
	.chown = oshfs_chown,
	.utimens = oshfs_utimens
};

int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &op, NULL);
}