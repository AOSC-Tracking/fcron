
/*
 * FCRON - periodic command scheduler 
 *
 *  Copyright 2000 Thibault Godouet <fcron@free.fr>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 *  The GNU General Public License can also be found in the file
 *  `LICENSE' that comes with the fcron source distribution.
 */

 /* $Id: fcrontab.c,v 1.17 2000-11-10 17:34:02 thib Exp $ */

/* 
 * The goal of this program is simple : giving a user interface to fcron
 * daemon, by allowing each user to see, modify, append or remove his 
 * fcrontabs. 
 * Fcron daemon use a specific formated format of file, so fcrontab generate
 * that kind of file from human readable files. In order allowing users to
 * see and modify their fcrontabs, the source file is always saved with the
 * formated one. 
 * Fcrontab makes a temporary formated file, and then sends a signal 
 * to the daemon to force it to update its configuration, remove the temp
 * file and save a new and final formated file.
 * That way, not the simple, allows the daemon to keep a maximum of 
 * informations like the time remaining before next execution, or the date
 * and hour of next execution.
 */

#include "fcrontab.h"

char rcs_info[] = "$Id: fcrontab.c,v 1.17 2000-11-10 17:34:02 thib Exp $";

void info(void);
void usage(void);
void sig_daemon(void);
pid_t read_pid(void);


/* command line options */
char rm_opt = 0;
char list_opt = 0;
char edit_opt = 0;
char reinstall_opt = 0;
char ignore_prev = 0;
int file_opt = 0;

#ifdef DEBUG
char debug_opt = 1;       /* set to 1 if we are in debug mode */
#else
char debug_opt = 0;       /* set to 1 if we are in debug mode */
#endif

char *user = NULL;
uid_t uid = 0;
uid_t asuid = 0;
gid_t asgid = 0;
uid_t fcrontab_uid = 0;
gid_t fcrontab_gid = 0;
char u_opt = 0;
char  *cdir = FCRONTABS;

char need_sig = 0;           /* do we need to signal fcron daemon */

char *orig_dir = NULL;
CF *file_base = NULL;
char buf[FNAME_LEN];
char file[FNAME_LEN];

/* needed by log part : */
char *prog_name = NULL;
char foreground = 1;
pid_t daemon_pid = 0;


void
info(void)
    /* print some informations about this program :
     * version, license */
{
    fprintf(stderr,
	    "fcrontab " VERSION_QUOTED " - user interface to daemon fcron\n"
	    "Copyright 2000 Thibault Godouet <fcron@free.fr>\n"
	    "This program is free software distributed WITHOUT ANY WARRANTY.\n"
            "See the GNU General Public License for more details.\n"
	);

    exit(EXIT_OK);

}


void
usage(void)
  /*  print a help message about command line options and exit */
{
    fprintf(stderr, 
	    "fcrontab [-u user] [-n] file\n"
	    "fcrontab [-u user] { -l | -r | -e | -z } [-n]\n"
	    "fcrontab -h\n"
	    "  -u user    specify user name.\n"
	    "  -l         list user's current fcrontab.\n"
	    "  -r         remove user's current fcrontab.\n"
	    "  -e         edit user's current fcrontab.\n"
	    "  -z         reinstall user's fcrontab from source code.\n"
	    "  -n         ignore previous version of file.\n"
	    "  -d         set up debug mode.\n"
	    "  -h         display this help message.\n"
	    "\n"
	);
    
    exit(EXIT_ERR);
}


pid_t
read_pid(void)
    /* return fcron daemon's pid if running.
     * otherwise return 0 */
{
    FILE *fp = NULL;
    pid_t pid = 0;
    
    if ((fp = fopen(PIDFILE, "r")) != NULL) {
	fscanf(fp, "%d", &pid);    
	fclose(fp);
    }

    return pid;
}


void
sig_daemon(void)
    /* send SIGHUP to daemon to tell him configuration has changed */
    /* SIGHUP is sent once 10s before the next minute to avoid
     * some bad users to block daemon by sending it SIGHUP all the time */
{
    time_t t = 0;
    int sl = 0;
    FILE *fp = NULL;
    int	fd = 0;
    struct tm *tm = NULL;


    t = time(NULL);
    tm = localtime(&t);
    
    if ( (sl = 60 - (t % 60) - 10) < 0 ) {
	if ( (tm->tm_min = tm->tm_min + 2) >= 60 ) {
	    tm->tm_hour++;
	    tm->tm_min -= 60;
	}
	snprintf(buf, sizeof(buf), "%02dh%02d", tm->tm_hour, tm->tm_min);
	sl = 60 - (t % 60) + 50;
    } else {
	if ( ++tm->tm_min >= 60 ) {
	    tm->tm_hour++;
	    tm->tm_min -= 60;
	}
	snprintf(buf, sizeof(buf), "%02dh%02d", tm->tm_hour, tm->tm_min);
    }

    fprintf(stderr, "Modifications will be take into account"
	    " at %s.\n", buf);


    /* try to create a lock file */
    if ((fd = open(FCRONTABS "/fcrontab.sig", O_RDWR|O_CREAT, 0644)) == -1
	|| ((fp = fdopen(fd, "r+")) == NULL) )
	die_e("can't open or create " PIDFILE);	
    
    if ( flock(fd, LOCK_EX|LOCK_NB) != 0 ) {
	debug("fcrontab is already waiting for signalling the daemon : exit");
	return;
    }


    (void) fcntl(fd, F_SETFD, 1);

    /* abandon fd and fp even though the file is open. we need to
     * keep it open and locked, but we don't need the handles elsewhere.
     */

    switch ( fork() ) {
    case -1:
	remove(FCRONTABS "/fcrontab.sig");
	die_e("could not fork : daemon as not been signaled");
	break;
    case 0:
	/* child */
	break;
    default:
	/* parent */
	return;
    }

    sleep(sl);
    
    remove(FCRONTABS "/fcrontab.sig");

    if ( (daemon_pid = read_pid()) == 0 )
	/* daemon is not running any longer : we exit */
	return ;

    if ( kill(daemon_pid, SIGHUP) != 0) {
	remove(PIDFILE);
	die_e("could not send SIGHUP to daemon (pid %d)", daemon_pid);
    }

}



void
xexit(int exit_val)
    /* launch signal if needed and exit */
{
    if ( need_sig == 1 ) {
	/* check if daemon is running */
	if ( (daemon_pid = read_pid()) != 0 )
	    sig_daemon();
	else
	    fprintf(stderr, "fcron is not running :\n  modifications will"
		    " be take into account at its next execution.\n");
    }

    exit(exit_val);

}


void
copy(char *orig, char *dest)
    /* copy orig file to dest */
{
    FILE *from = NULL, *to = NULL;
    char c;

    if (uid != 0 && setuid(uid) < 0)
	die_e("setuid(uid)");

    if ( (from = fopen(orig, "r")) == NULL) {
	error_e("copy: orig");
	return ;
    }
    if (uid != 0 && setuid(fcrontab_uid) < 0) {
	fclose(from);
	die_e("setuid(fcrontab_uid)");
    }

    if ((to = fopen(dest, "w")) == NULL) {
	error_e("copy: dest");
	return ;
    }

    while ( (c = getc(from)) != EOF )
	if ( putc(c, to) == EOF ) {
	    fprintf(stderr, "Error while copying file. Aborting.\n");
	    xexit(ERR);
	}

    fclose(from);
    fclose(to);
    
}


int
remove_fcrontab(char rm_orig)
    /* remove user's fcrontab and tell daemon to update his conf */
{

    if ( rm_orig )
	explain("removing %s's fcrontab", user);

    /* remove source and formated file */
    if ( (rm_orig && remove(buf)) != 0 || remove(user) != 0) {
	    
	/* try to remove the temp file in case he has not
	 * been read by fcron daemon */
	snprintf(buf, sizeof(buf), "new.%s", user);
	if ( remove(buf) != 0 ) {

	    if ( errno == ENOENT )
		return ENOENT;
	    else
		error_e("could not removing %s", buf);		
	}

    }

    /* finally create a file in order to tell the daemon
     * a file was removed, and launch a signal to daemon */
    { 
	FILE *f;
	snprintf(buf, sizeof(buf), "rm.%s", user);
	f = fopen(buf, "w");
	fclose(f);

	need_sig = 1;

    }

    return OK;

}


void
write_file(char *file)
{

    if (ignore_prev == 1)
	/* if user wants to ignore previous version, we remove it *
	 * ( fcron daemon remove files no longer wanted before
	 *   adding new ones ) */
	remove_fcrontab(0);

    if ( file_base->cf_line_base == NULL ) {
	/* no entries */
	explain("%s's fcrontab contains no entries", user);
	remove_fcrontab(0);
    } 
    else {
	/* write that list in a temp file on disk */
	snprintf(buf, sizeof(buf), "new.%s", user);
	save_file(buf);
    }

    /* copy original file to FCRONTABS dir */
    snprintf(buf, sizeof(buf), "%s.orig", user);
    copy(file, buf);


}

int
make_file(char *file)
{

    explain("installing file '%s' for user %s", file, user);

    /* read file and create a list in memory */
    switch ( read_file(file, user) ) {
    case 2:
    case OK:

	write_file(file);

	/* free memory used to store the list */
	delete_file(user);

	/* tell daemon to update the conf */
	need_sig = 1;
	break;

    case ERR:
	return ERR;
    }

    return OK;
    
}


void
list_file(char *file)
{
    FILE *f = NULL;
    char c;

    explain("listing %s's fcrontab", user);

    if ( (f = fopen(file, "r")) == NULL ) {
	if ( errno == ENOENT ) {
	    explain("user %s has no fcrontab.", user);
	    return ;
	}
	else
	    die_e("User %s could not read file '%s'", user, file);
    }
    else {

	while ( (c = getc(f)) != EOF )
	    putchar(c);

	fclose(f);

    }

}

void
edit_file(char *buf)
    /* copy file to a temp file, edit that file, and install it
       if necessary */
{
    char *editor = NULL;
    pid_t pid;
    int status;
    struct stat st;
    time_t mtime = 0;
    char tmp[FNAME_LEN];
    FILE *f, *fi;
    int file = 0;
    char c;
    char correction = 0;

    explain("fcrontabs : editing %s's fcrontab", user);	

    if ( (editor = getenv("VISUAL")) == NULL )
	if( (editor = getenv("EDITOR")) == NULL )
	    editor = EDITOR;
	
    sprintf(tmp, "/tmp/fcrontab.%d", getpid());

    /* create a temp file with user's permissions */
    if (uid != 0 && setuid(uid) != 0 ) 
  	die_e("Could not change uid"); 

    if ( (file = open(tmp, O_CREAT|O_EXCL|O_WRONLY, 0600)) == -1 ) {
	error_e("could not create file %s", tmp);
	goto exiterr;
    }
    if ( (fi = fdopen(file, "w")) == NULL ) {
	error_e("could not fdopen");
	goto exiterr;
    }
    if (uid != 0 && setuid(fcrontab_uid) != 0 ) 
  	die_e("Could not change uid"); 

    /* copy user's fcrontab (if any) to a temp file */
    if ( (f = fopen(buf, "r")) == NULL ) {
	if ( errno != ENOENT ) {
	    error_e("could not open file %s uid:%d euid:%d", buf, getuid(), geteuid());
	    goto exiterr;
	}
	else
	    fprintf(stderr, "no fcrontab for %s - using an empty one\n",
		    user);
    }
    else { 
	/* copy original file to temp file */
	while ( (c=getc(f)) != EOF )
	    putc(c, fi);
	fclose(f);
    }

    /* if root has given -u option, chown the file in order to be editable
     * by the user given with -u option */
    if (u_opt == 1) {
	/* we need to have euid set to root */
	if (uid == 0 && setreuid(-1, 0) != 0)
	    die_e("Could not change euid to root");	
	if ( fchown(file, asuid, asgid) != 0 )
	    die_e("Could not chown %s", tmp);
	if (uid == 0 && setreuid(0, fcrontab_uid) != 0)
	    die_e("Could not change euid to root");	
    }

    fclose(fi);
    close(file);

    do {

	if ( stat(tmp, &st) == 0 )
	    mtime = st.st_mtime;
	else {
	    error_e("could not stat '%s'", buf);
	    goto exiterr;
	}

	switch ( pid = fork() ) {
	case 0:
	    /* child */
	    if (uid == 0)
		/* we need to become root to perform the gid and uid changes */
		if (setreuid(0, 0) != 0)
		    error_e("setreuid(0, 0)");
  	    if (setregid(asgid, asgid) < 0) {
  		error_e("setregid(gid, gid)");
  		goto exiterr;
  	    }
	    if (setreuid(asuid, asuid) < 0) {
		error_e("setreuid(uid, uid)");
		goto exiterr;
	    }
	    execlp(editor, editor, tmp, NULL);
	    error_e(editor);
	    goto exiterr;

	case -1:
	    error_e("fork");
	    goto exiterr;

	default:
	    /* parent */
	    break ;
	}
	    
	/* only reached by parent */
	wait4(pid, &status, 0, NULL);
	if ( ! WIFEXITED(status) ) {
	    fprintf(stderr, "Editor exited abnormally:"
		    " fcrontab is unchanged.\n");
	    goto exiterr;
	}

	/* if root has given -u option, chown the file in order to be editable
	 * by the user given with -u option */
	if (u_opt == 1) {
	    /* we need to have euid set to root */
	    if (uid == 0 && setreuid(-1, 0) != 0)
		die_e("Could not change euid to root");	
	    if ( chown(tmp, fcrontab_uid, fcrontab_gid) != 0 )
		die_e("Could not chown %s", tmp);
	    if (uid == 0 && setreuid(0, fcrontab_uid) != 0)
		die_e("Could not change euid to root");	
	}

	/* check if file has been modified */
	if ( stat(tmp, &st) != 0 ) {
	    error_e("could not stat %s", tmp);
	    goto exiterr;
	}
    
	else if ( st.st_mtime > mtime || correction == 1) {

	    correction = 0;

	    switch ( read_file(tmp, user) ) {
	    case ERR:
		goto exiterr;
	    case 2:
		fprintf(stderr, "\nFile contains some errors. "
			"Ignore [i] or Correct [c] ? ");
		/* the 2nd getchar() is for the newline char (\n) */
		while ( (c = getchar()) && getchar() && c != 'i' && c != 'c' )
		    fprintf(stderr, "Please press c to correct, "
			    "or i to ignore: ");
		if ( c == 'c' ) {
		    /* free memory used to store the list */
		    delete_file(user);
		    correction = 1;
		}
		break;
	    default:
		break;
	    }

	}
	else {
	    fprintf(stderr, "Fcrontab is unchanged :"
		    " no need to install it.\n"); 
	    goto end;
	}

    } while ( correction == 1);

    write_file(tmp);
    
    /* free memory used to store the list */
    delete_file(user);
    
    /* tell daemon to update the conf */
    need_sig = 1;
    
  end:
    if ( remove(tmp) != 0 )
	error("could not remove %s", tmp);
    xexit (EXIT_OK);

  exiterr:
    if ( remove(tmp) != 0 )
	error("could not remove %s", tmp);
    xexit (EXIT_ERR);

}


void
install_stdin(void)
    /* install what we get through stdin */
{
    FILE *tmp_file = NULL;
    char tmp[FNAME_LEN];
    register char c;
	    	    
    /* create a temp file with user's permissions */
    if (uid != 0 && setuid(uid) < 0)
	die_e("setuid(uid)");

    sprintf(tmp, "/tmp/fcrontab.%d", getpid());
    if( (tmp_file = fopen(tmp, "w")) == NULL )
	fprintf(stderr, "Could not open2 '%s': %s\n", tmp,
		strerror(errno));

    while ( (c = getc(stdin)) != EOF )
	putc(c, tmp_file);

    fclose(tmp_file);

    if (uid != 0 && setuid(fcrontab_uid) < 0) {
	error_e("setuid(fcrontab_uid)");
	goto exiterr;
    }

    if ( make_file(tmp) == ERR )
	goto exiterr;
    else {
	remove(tmp);
	xexit ( EXIT_OK );
    }

  exiterr:
    remove(tmp);
    xexit ( EXIT_ERR );

}

void
reinstall(char *buf)
{
    int i = 0;

    explain("reinstalling %s's fcrontab", user);

      /* create a temp file with user's permissions */
/*      if (uid != 0 && setuid(uid) < 0) */
/*  	die_e("setuid(uid)"); */

    if ( (i = open(buf, O_RDONLY)) < 0) {
	if ( errno == ENOENT ) {
	    fprintf(stderr, "Could not reinstall: user %s has no fcrontab\n",
		    buf);
	}
	else
	    fprintf(stderr, "Could not open '%s': %s\n", buf,
		    strerror(errno));

	exit (EXIT_ERR);
    }
/*      if (uid != 0 && setuid(fcrontab_uid) < 0) { */
/*  	close(i); */
/*  	die_e("setuid(fcrontab_uid)"); */
/*      } */

    close(0); dup2(i, 0);
    close(i);

    install_stdin();

}

void
parseopt(int argc, char *argv[])
  /* set options */
{

    char c;
    extern char *optarg;
    extern int optind, opterr, optopt;

    /* constants and variables defined by command line */

    while(1) {
	c = getopt(argc, argv, "u:lrezdnhV");
	if (c == -1) break;
	switch (c) {

	case 'V':
	    info(); break;

	case 'h':
	    usage(); break;

	case 'u':
	    user = strdup2(optarg) ; 
	    if (getuid() != 0) {
		fprintf(stderr, "must be privileged to use -u\n");
		xexit(EXIT_ERR);
	    }
	    break;

	case 'd':
	    debug_opt = 1; break;

	case 'l':
	    list_opt = 1;
	    rm_opt = 0;
	    edit_opt = 0;
	    reinstall_opt = 0;
	    break;

	case 'r':
	    list_opt = 0;
	    rm_opt = 1;
	    edit_opt = 0;
	    reinstall_opt = 0;
	    break;

	case 'e':
	    list_opt = 0;
	    rm_opt = 0;
	    edit_opt = 1;
	    reinstall_opt = 0;
	    break;

	case 'z':
	    list_opt = 0;
	    rm_opt = 0;
	    edit_opt = 0;
	    reinstall_opt = 1;
	    break;

	case 'n':
	    ignore_prev = 1;
	    break;
	    
	case ':':
	    fprintf(stderr, "(setopt) Missing parameter");
	    usage();

	case '?':
	    usage();

	default:
	    fprintf(stderr, "(setopt) Warning: getopt returned %c", c);
	}
    }

    if ( user == NULL ) {
	/* we need to strdup2 the value given by getenv() because we free
	 * file->cf_user in delete_file */
	if ((user = strdup2(getenv("USER"))) == NULL) {
	    fprintf(stderr, "Could not get user name.\n");
	    xexit(EXIT_ERR);
	}
	asuid = getuid();
	asgid = getgid();
    }
    else {
	struct passwd *pass;
	if ( ! (pass = getpwnam(user)) )
	    die("user '%s' is not in passwd file. Aborting.", user);
	asuid = pass->pw_uid;
	asgid = pass->pw_gid;
	u_opt = 1;
    }

    if ( ! is_allowed(user) ) {
	die("User '%s' is not allowed to use %s. Aborting.",
	    user, prog_name);	    

    }

    if (optind < argc)
	file_opt = optind;
}


int
main(int argc, char **argv)
{
    struct passwd *pass;

    memset(buf, 0, sizeof(buf));
    memset(file, 0, sizeof(file));

    if (strrchr(argv[0],'/')==NULL) prog_name = argv[0];
    else prog_name = strrchr(argv[0],'/')+1;

    /* interpret command line options */
    parseopt(argc, argv);

    uid = getuid();

    if ( ! (pass = getpwnam(USERNAME)) )
	die("user '%s' is not in passwd file. Aborting.", USERNAME);
    fcrontab_uid = pass->pw_uid;
    fcrontab_gid = pass->pw_gid;
    if (uid != 0 && setuid(fcrontab_uid) != 0 ) 
  	die_e("Could not change uid to " USERNAME); 
/*      if (setgid(fcrontab_gid) != 0) */
/*    	die_e("Could not change gid to " GROUPNAME);  */
	

    /* this program is seteuid : we set default permission mode
     * to  600 for security reasons */
    umask(066);

    /* get current dir */
    if ( (orig_dir = getcwd(NULL, 0)) == NULL )
	error_e("getcwd");

    /* change directory */
    if (chdir(cdir) != 0) {
	error_e("Could not chdir to " FCRONTABS );
	xexit (EXIT_ERR);
    }

    snprintf(buf, sizeof(buf), "%s.orig", user);

    /* determine what action should be taken */
    if ( file_opt && ! list_opt && ! rm_opt && ! edit_opt && ! reinstall_opt) {

	if ( strcmp(argv[file_opt], "-") == 0 )

	    install_stdin();

	else {

	    if ( *argv[file_opt] != '/' )
		/* this is just the file name, not the path : complete it */
		snprintf(file,sizeof(file),"%s/%s",orig_dir,argv[file_opt]);
	    else
		strncpy(file, argv[file_opt], sizeof(file));

	    if (make_file(file) == OK)
		xexit ( EXIT_OK );
	    else
		xexit ( EXIT_ERR );

	}

    } else if ( list_opt + rm_opt + edit_opt + reinstall_opt != 1 )
	usage();


    /* remove user's entries */
    if ( rm_opt == 1 ) {

	if ( remove_fcrontab(1) == ENOENT )
	    fprintf(stderr, "no fcrontab for %s\n", user);

	xexit (EXIT_OK);
    }


    /* list user's entries */
    if ( list_opt == 1 ) {

	list_file(buf);

	xexit(EXIT_OK);

    }


    /* edit user's entries */
    if ( edit_opt == 1 ) {

	edit_file(buf);

	xexit(EXIT_OK);

    }

    /* reinstall user's entries */
    if ( reinstall_opt == 1 ) {

	reinstall(buf);

	xexit(EXIT_OK);

    }



    /* never reach */
    return EXIT_OK;
}
