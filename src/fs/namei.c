#include <param.h>
#include <x86.h>
#include <proto.h>
#include <proc.h>
// 
#include <buf.h>
#include <conf.h>
//
#include <super.h>
#include <inode.h>


/* 
 * fetch an inode number from a single directory file (via a locked inode, make sure
 * it's an directory).
 * returns 0 on fail.
 * */
uint find_entry(struct inode* ip, char *name, uint len){
    struct inode *nip;
    struct buf *bp;
    struct dirent *dep;
    int i, j, bn=0, ino=0;

    if ((ip->i_mode & S_IFMT)!=S_IFDIR) {
        syserr(EFAULT);
        return 0;
    }

    for(i=0; i<ip->i_size/BSIZE+1; i++){
        bn = bmap(ip, i, 0);
        bp = bread(ip->i_dev, bn);
        dep = (struct dirent *)bp->b_data;
        for(j=0; j<BSIZE/(sizeof(struct dirent))+1; j++) {
            if (0==strncmp(name, dep[j].d_name, len)){
                ino = dep[j].d_ino;
                brelse(bp);
                return ino;
            }
        }
        brelse(bp);
    }
    return 0;
}

/*
 * assign a new directory entry with a given inode. the given inode number
 * equals 0, it's a remove.
 * note: this routine do NOT check the existence of the given name.
 * */
uint link_entry(struct inode *dip, char *name, uint ino){
    struct buf *bp;
    struct dirent de;
    int i, r, off;

    if ((dip->i_mode & S_IFMT)!=S_IFDIR) {
        syserr(EFAULT);
        return 0;
    }

    for (off=0; off < dip->i_size; dip+=sizeof(struct dirent)){
        r = readi(dip, &de, sizeof(struct dirent));
        if (r != sizeof(struct dirent)){
            panic("bad inode");
        }
        if (de.d_ino == 0) {
            break;
        }
    }
    strncpy(de.d_name, name, NAMELEN);
    de.d_ino = ino;
    r = writei(dip, &de, off, sizeof(struct dirent));
    if (r != sizeof(struct dirent)){
        panic("bad inode");
    }
    return ino;
}

/*
 * returns a locked inode.
 * */
struct inode* _namei(char *path, uchar parent, uchar creat){
    struct inode *wip=NULL, *cdp=NULL;
    uint ino, offset;
    char* tmp;

    // if path starts from root
    // note if p_cdir==NULL, it's an early initialized process, 
    // set it's root directory here.
    if (*path == '/') {
        wip = iget(rootdev, ROOTINO);
        current->p_cdir = wip;
    }
    else {
        cdp = current->p_cdir;
        wip = iget(cdp->i_dev, cdp->i_num);
    }

    // while there is more path name with '/'
    while (*path != '\0') {
        if (*path=='/'){
            path++;
            continue;
        }
        // if working inode is root and componet is ".."
        if ((wip->i_num==ROOTINO) && (strncmp(path, "..", 2)==0)) {
            continue;
        }
        // wip must be a directory, TODO: check access
        if ((wip->i_mode & S_IFMT)!=S_IFDIR) {
            syserr(EACCES);
            return NULL;
        }
        tmp = strchr(path, '/');
        offset = (tmp==NULL) ? strlen(path): (tmp-path);
        ino = find_entry(wip, path, offset);
        // if not found
        if (ino <= 0){
            syserr(ENOENT);
            return NULL;
        }
        iput(wip);
        wip = iget(wip->i_dev, ino);
        path += offset;
    }
    return wip;
}

struct inode* namei(char *path, uchar creat){
    return _namei(path, 0, creat);
}

struct inode* namei_parent(char *path){
    return _namei(path, 1, 0);
}
