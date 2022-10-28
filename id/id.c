#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>

typedef struct
{
    char* user;
    int iscurrent;
} args_t;

const char* PROGNAME = NULL;    

static int
error(char* fmt, ...)
{
	fprintf(stderr, "%s: ", PROGNAME);

	va_list args = {};
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	return 1;
}

static int
parse_args(int argc, char* argv[], args_t* args)
{ 
    if (argc > 2)
    	return error("%s\n", "multiple users are not supported");

    if (argc == 2)
    {
        args->user = argv[1];
        args->iscurrent = 0;

        return 0;
    }

    args->iscurrent = 1;

    return 0;
}

int
main(int argc, char* argv[])
{
    PROGNAME = argv[0];

    args_t args = {};
    if (parse_args(argc, argv, &args) != 0)
        return 1;

    /* getting UID */
    struct passwd* user_info = NULL;
    uid_t uid = (uid_t) -1;
    if (args.iscurrent)
    {
        uid = getuid();
        
        user_info = getpwuid(uid);
	    if (!user_info)
	        return error("%s\n", (errno) ? strerror(errno) : "no such user");
    }
    else
    {
        user_info = getpwnam(args.user);
	    if (!user_info)
	        return error("%s\n", (errno) ? strerror(errno) : "no such user");
        
        uid = user_info->pw_uid;
    }

    printf("uid=%d(%s) ", (int) uid, user_info->pw_name);

    /* getting GID */
	struct group* group_info = NULL; 
    gid_t gid = (gid_t) -1;	
    if (args.iscurrent)
    {
        gid = getgid();
        
        group_info = getgrgid(gid);
	    if (!group_info)
	        return error("%s\n", (errno) ? strerror(errno) : "no group for process gid");
    }
    else
    {
        group_info = getgrnam(args.user);
	    if (!group_info)
	        return error("%s\n", (errno) ? strerror(errno) : "no group for process gid");
        
        gid = group_info->gr_gid;
    }

    printf("gid=%d(%s) ", (int) gid, group_info->gr_name);

    /* getting supplementary groups */
	gid_t groups_gid[NGROUPS_MAX];
	
    int n_groups = NGROUPS_MAX;
    
    if (args.iscurrent)
    {
        n_groups = getgroups(NGROUPS_MAX, groups_gid);
	    if (n_groups == -1)
            return error("%s\n", strerror(errno));
    }
    else
    {
        n_groups = getgrouplist(args.user, gid, groups_gid, &n_groups);
        if (n_groups == -1)
            return error("%s\n", strerror(errno)); 
    }

    printf("groups=");
	for (int i = 0; i < n_groups; i++)
    {	
        group_info = getgrgid(groups_gid[i]);
		if (!group_info)
			return error("%s\n", (errno) ? 
                                  strerror(errno) : 
                                  "no group for process supplementary gid");
        printf("%d(%s) ", (int) groups_gid[i], group_info->gr_name);
    }

    printf("\n");

    return 0;
}

