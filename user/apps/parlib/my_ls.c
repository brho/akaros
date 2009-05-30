/* Reinventing the wheel - and why not?!		*/
/* An implementation of ls						*/
/* (C) 2004 Kevin Waldron						*/
/* http://www.zazzybob.com						*/
/*												*/
/* Distributed under the terms of the			*/
/* GNU General Public Licence					*/
/*												*/
/* Current emulates ls -1 (one) for a given		*/
/* list of directories and files				*/
/* supports -i (inode)							*/
/*          -d (directory)						*/
/*          -l (long listing)                   */
/*          -a (all)                            */
/*          -A (all, no . or .. )               */
/* assumes -1(one) by default					*/

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>

#define MAXDIRS 20

void print_usage(void);
void print_it(struct stat, char *);

int i_flag = 0;
int d_flag = 0;
int l_flag = 0;
int a_flag = 0;
int A_flag = 0;  /* case sensitivity, woohoo! */

char *username;
char *groupname;
		
int main( int argc, char *argv[] )
{
  int c;
  extern char *optarg;
  extern int optind, optopt;

  int errflag = 0;
  DIR *p_dir;
  struct stat st;
  struct dirent *en;

  /* malloc our user/groupnames to save hassle later */
  username = (char *)malloc(256);
  *username='\0';
  groupname = (char *)malloc(256);
  *groupname='\0';
  
  while ( ( c = getopt( argc, argv, "iaAld" ) ) != -1 )
  {
    switch( c ) {
	  case 'a':
		 a_flag = 1;
	     break;
	  case 'A':
		 A_flag = 1;
	     break;
      case 'i':
         i_flag = 1;
         break;
	  case 'd':
		 d_flag = 1;
	     break;
	  case 'l':
		 l_flag = 1;
	     break;
      case '?':
         errflag++;
         break;
    }
  }

  /* incorrect option passed - print usage && exit */
  if ( errflag != 0 )
  {
     print_usage();
     exit( 1 );
  }

  char *dirs[MAXDIRS];
  int counter = 0;

  /* no dirs passed as args */
  if ( optind == argc )
  {
     dirs[0] = (char *)malloc(2);
     strcpy( dirs[0], "." );
     counter = 1;
  }
  else
  {
    for ( ; optind < argc; optind++ ) {

      if ( optind > MAXDIRS ) {
        fprintf( stderr, "Too many arguments....\n" );
        exit( 1 );
      }

      dirs[counter] = (char *)malloc(strlen(argv[optind])+1);
      strcpy( dirs[counter], argv[optind] );
      counter++;
    }
  } 

  int i = 0;
  for ( i = 0 ; i < counter; i++ )
  {
	 char buf[256];
	 getcwd( buf, 255 );
	 char *p_path;
	 char *p_file;
	 char *tmp_c;
				
	 p_file = (char *)malloc(256); 
	 p_path = (char *)malloc(256); 
	 tmp_c = (char *)malloc(2);	 

	 *p_file = '\0';
	 *p_path = '\0';
	 *tmp_c = '\0';
     
	 if ( ( p_dir = opendir( dirs[i]) ) == NULL )
     {
		/* if dir[i] contains ./ or ../ then we must have somekind of relative path, so... */
		int result;
		if ( ( result = strstr( dirs[i], "./" ) != NULL ) ||
			 ( result = strstr( dirs[i], "../" ) != NULL ) )
		{
			char *loc;
			loc = strrchr( dirs[i], '/' );



			int k;
			for ( k = 0 ; k < ( loc - dirs[i] ); k++ )
			{
				*(tmp_c) = dirs[i][k];
				*(tmp_c+1) = '\0';
				strcat( p_path, tmp_c );
			}
			k++; /*strip leading /*/
			for ( ; k < strlen( dirs[i] ); k++ )
			{
				*(tmp_c)= dirs[i][k];
				*(tmp_c+1) = '\0';
				strcat( p_file, tmp_c );
			}

			chdir( p_path );
			struct stat tmpstat;
			int r;
			if ( ( r = lstat( p_file, &tmpstat ) ) != 0 )
			{
			    fprintf( stderr, "\n%s: not found\n", p_file );
			}
			else
			{
				lstat( p_file, &st );
				printf( "\n%s:\n", dirs[i] );
				print_it( st, p_file );
  			}
			
			chdir( buf );

		}
		else
		{
			/* either full path or current directory */
			struct stat tmpstat;
			int r;
			if ( ( r = lstat( dirs[i], &tmpstat ) ) != 0  )
			{
			    fprintf( stderr, "\n%s: not found\n", dirs[i] );
			}
			else
			{
				lstat( dirs[i], &st );
				print_it( st, dirs[i] );
			}
		}
     } 
	 else if ( d_flag != 0 )
	 {
		 /* don't descend into directory */
		 lstat( dirs[i], &st );
		 print_it( st, dirs[i] );
	 }
	 else 
	 {
		/* descend into directory as normal */

		/* if more than one dir specified then print header */
		( counter > 1 ? printf( "\n%s:\n", dirs[i] ) : printf( "" ) );

  		chdir( dirs[i] );
	 
		while ( ( en = readdir( p_dir ) ) != NULL )
		{
		   lstat( en->d_name, &st );
		   print_it( st, en->d_name );
		}
		chdir( buf );
		closedir( p_dir );
	 }

	free( p_file );
	free( p_path );
	free( tmp_c ); 
  }
  int iDealloc;
  /* Cycle through dir array and free pointers - to be nice */
  for ( iDealloc = 0; iDealloc < i; iDealloc++ )
  {
     free( dirs[ iDealloc ] );
  }
  free( username );
  free( groupname );
  exit( 0 );
}

/* print_it gets data from the struct and formats it */
void print_it(struct stat stst, char *dn )
{
	    if ( A_flag != 0 )
	    {
			/* print dot files but not . or .. */
			if ( ( strcmp( dn, "." ) == 0 ) || ( strcmp( dn, ".." ) == 0 ) )
			{
				/* need this otherwise current directory will be ignored for ls -d */
				if ( d_flag == 0 )
				{
					/* ignore */
					return;
				} 
			}
		} 
	    /* supress all dot files */
	    if ( ( a_flag == 0 ) && ( A_flag == 0 ) )
	    {
			/* if first char is . */
			if ( dn[0] == '.' )
			{
				/* need this otherwise current directory will be ignored for ls -d */
				if ( d_flag == 0 )
				{
					/*ignore*/
					return;
				}
			}
	    }
		/* ELSE, print the lot.... */
	    
		/* set various mode flags */
		char *ft = "-\0";
		char *ur = "-\0";
		char *uw = "-\0";
		char *ux = "-\0";
		char *gr = "-\0";
		char *gw = "-\0";
		char *gx = "-\0";
		char *or = "-\0";
		char *ow = "-\0";
		char *ox = "-\0";

		if ( S_ISDIR( stst.st_mode ) ) {
			ft = "d";
		}
		if ( S_ISBLK( stst.st_mode ) ) {
			ft = "b";
		}
		if ( S_ISCHR( stst.st_mode ) ) {
			ft = "c";
		}
		if ( S_ISFIFO( stst.st_mode ) ) {
			ft = "f";
		}
		if ( S_ISLNK( stst.st_mode ) ) {
			ft = "l";
		}
		if (  stst.st_mode & S_IRUSR )
		{
			ur = "r";
		}
		if ( stst.st_mode & S_IWUSR )
		{
			uw = "w";
		}
		if ( stst.st_mode & S_IXUSR )
		{
			ux = "x";
		}
		if ( stst.st_mode & S_IRGRP )
		{
			gr = "r";
		}
		if ( stst.st_mode & S_IWGRP )
		{
			gw = "w";
		}
		if ( stst.st_mode & S_IXGRP )
		{
			gx = "x";
		}
		if ( stst.st_mode & S_IROTH )
		{
			or = "r";
		}
		if ( stst.st_mode & S_IWOTH )
		{
			ow = "w";
		}
		if ( stst.st_mode & S_IXOTH )
		{
			
			ox = "x";
		}
		if ( stst.st_mode & S_ISVTX )
		{
				if ( stst.st_mode & S_IXOTH )
				{
					ox = "t";
				}
				else
				{
					ox = "T";
				}
		}
		if ( stst.st_mode & S_ISUID )
		{
				if ( stst.st_mode & S_IXUSR )
				{
					ux = "s";
				}
				else
				{
					ux = "S";
				}
		}
		if ( stst.st_mode & S_ISGID )
		{
				if ( stst.st_mode & S_IXGRP )
				{
					gx = "s";
				}
				else
				{
					gx = "S";
				}
		}

		/* check this - is it doing what we want? */
		unsigned long ino = stst.st_ino;

	    unsigned long nl = stst.st_nlink;
		unsigned long fs = stst.st_size;

		struct passwd *pw;
		struct group *grp;

		uid_t uid = stst.st_uid;
		gid_t gid = stst.st_gid;

        pw = getpwuid(uid);
		grp = getgrgid(gid);

	    
		strncpy( username, pw->pw_name, 9 );
		strcat( username, "\0");
		strncpy( groupname, grp->gr_name, 9 );
		strcat( groupname, "\0");

		/* time calc */
		char m_time[81];
		time_t t;
		struct tm *tm;
		tm = malloc(sizeof(struct tm));
		t = stst.st_mtime;
		tm = localtime( &t );
	    strftime(m_time,80,"%d-%m-%Y %H:%M",tm);

		/* preceed listing with inode number */
        if ( i_flag != 0 )
        {
			printf( "%7lu ", ino );
        }
        if ( l_flag != 0)
        {
			/* long listing....*/
			printf( "%s%s%s%s%s%s%s%s%s%s%3lu %9s %9s", ft, ur, uw, ux,
			                                      gr, gw, gx,
			                                      or, ow, ox, nl, username, 
			                                      groupname);
			if ( ( ft == "c" ) || ( ft == "b" ) ) /* Must be a device file */
			{
				int min = minor(stst.st_rdev);
				int maj = major(stst.st_rdev);
				printf( "%3d, %3d", maj, min );
			}
			else
			{
				printf( "%8lu", fs );
			}
			printf( " %s ", m_time );
        }
		/* print file name under all circumstances */
		printf ( "%s\n", dn );

		free( tm );
}

void print_usage(void)
{
	char *usage[] = { "my_ls - (C) 2004 Kevin Waldron",
		              "Distributed under the terms of the GNU General Public Licence",
		              "USAGE:", "my_ls [options] [files, directories]",
		              "  -a  Print listing for all files",
		              "  -A  Print listing for all files, except . and ..",
		              "  -d  Print information about directory entry", 
		              "  -i  Preceed each listing with the inode number",
				      "  -l  List output in long format" };

    int i = 0;
	for ( i = 0 ; i < 9 ; i++ )
	{
		printf( "%s\n", *(usage+i) );
	}
}
