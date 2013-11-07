#include "../nplib/np_header.h"
#include "../nplib/np_lib.h"

#define SERV_PORT 9877
#define max(x, y) ((x>y)? (x) : (y))
#define min(x, y) ((x>y)? (y) : (x))

void str_cli( FILE*, int );
void cli_write( int fd, char* msg, int msg_length );

int main(int argc, char** argv)
{
  if(argc != 2)
    err_quit( "usage: client <IPaddress>" );

  int channelfd, serv_size;
  struct sockaddr_in servaddr;
  char servIP[ INET_ADDRSTRLEN+1 ];

  bzero( &servaddr, sizeof(servaddr) );
  servaddr.sin_family = AF_INET;
  Inet_pton( AF_INET, argv[1], &servaddr.sin_addr );
  servaddr.sin_port = htons(SERV_PORT);

  channelfd = Socket( AF_INET, SOCK_STREAM, 0 );
  Connect( channelfd, (const SA*)&servaddr, sizeof(servaddr) );

  getpeername( channelfd, (SA*)&servaddr, &serv_size );
  Inet_ntop( AF_INET, &servaddr.sin_addr, servIP, sizeof(servIP) );
  write( channelfd, servIP, INET_ADDRSTRLEN+1 );

  str_cli( stdin, channelfd );

  Close( channelfd );

  return 0;
}

void str_cli( FILE* infile, int channelfd )
{
  char recvline[MAXLINE], sendline[MAXLINE];
  fd_set rset;
  int max_fd, bytes_read, stdineof = 0;
  FD_ZERO( &rset );
  
  static int whilecount = 0;
  while(1){
    if( stdineof == 0 )
      FD_SET( STDIN_FILENO, &rset );
    FD_SET( channelfd, &rset );
    max_fd = max( STDIN_FILENO, channelfd ) + 1;

    //printf( "%dth time in loop, (stdin,fd)=(%d,%d)\n", ++whilecount, FD_ISSET(STDIN_FILENO, &rset), FD_ISSET(channelfd, &rset) );

    //blocking for either user or socket inputs
    Select( max_fd, &rset, NULL, NULL, NULL);

    //If user inputs
    if( FD_ISSET(STDIN_FILENO, &rset) )
      //if input ends with new line
      if( Fgets( sendline, MAXLINE, infile ) != NULL ) {
        if( strcmp( sendline, "/quit\n" ) != 0 )
          Writen( channelfd, sendline, strlen(sendline)+1 );
        else{
        Shutdown( channelfd, SHUT_WR );
        stdineof = 1;
        FD_CLR( STDIN_FILENO, &rset );
      }
      //if input ends with EOF
      } else{
        Shutdown( channelfd, SHUT_WR );
        stdineof = 1;
        FD_CLR( STDIN_FILENO, &rset );
      }
 
    //if socket inputs
    if( FD_ISSET(channelfd, &rset ) ) {
      //if nothing is read
      if( (bytes_read = Read( channelfd, recvline, MAXLINE )) == 0 )
        //normal termination
        if( stdineof == 1 )
          return;
        //abnormal termination
        else
          err_sys( "str_cli: server terminate prematurely" );
      //if something is read
      cli_write( STDOUT_FILENO, recvline, bytes_read ); 
    }
  }
}

void cli_write( int fd, char* buf, int buf_length )
{
  char* msg;
  int msg_length;
  while( ((msg = strtok(buf,"\n" ))!=NULL) && (buf_length > 0) ){
	msg_length = strlen(msg);
	msg[msg_length] = '\n';
	msg_length++;
	buf_length -= msg_length;
    if( strncmp( msg, "/msg ", 5 )==0 ){
      write( fd, msg+5, msg_length-5 );
    } else if( strncmp( msg, "/serv ", 6 )==0 ){
      write( fd, "[server]: ", 10 );
      write( fd, msg+6, msg_length-6 );
    } else if( strncmp( msg, "/private ", 9 )==0 ){
      write( fd, "[private]: ", 11 );
      write( fd, msg+9, msg_length-9 );
    }
  }
}
