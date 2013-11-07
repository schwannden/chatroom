#include "../nplib/np_header.h"
#include "../nplib/np_lib.h"

#define BACKLOG 10
#define SERV_PORT 9877
#define NAMESIZE 12

//define my own error code, starting from 256 to avoid collision with system error code
#define ENAMEANON    256
#define ENAMEUSED    257
#define ENAMEFORMAT  258
#define ETOANON      259
#define EFROMANON    260
#define ENOUSER      261

static verbose;

struct record {
  int size;
  int fd[ FD_SETSIZE ];
  int port[ FD_SETSIZE ];
  char addrstr[ FD_SETSIZE ][ INET_ADDRSTRLEN ];
  char name[ FD_SETSIZE ][ NAMESIZE + 1 ];
};

void record_init( struct record* ptr_record );
int record_exist( int i, struct record* ptr_record );
int record_add( int in_fd, struct sockaddr_in* in_addr, struct record* ptr_record );
void record_delete( int i, struct record* ptr_record );
void broadcast( const char* msg, size_t n, struct record* ptr_record );
void server_react( char* buf, int bytes_read, int src, struct record* ptr_record );
char* convert_name( char* buf, int src, struct record* ptr_record );
void broadcast_noback( const char* msg, size_t n, int src, struct record* ptr_record );
int record_findbyname( const char* name, struct record* ptr_record );
int record_findbyfd( int fd, struct record* ptr_record );


int main(int argc, char** argv)
{
  int i, listenfd, connfd, connfd_temp, maxfd, on=1;
  struct record client_record;
  size_t client_ready, bytes_read, client_record_size;
  char buf[MAXLINE];
  fd_set rset, rset_new;
  struct sockaddr_in cliaddr, servaddr;
  socklen_t cliaddrsize, servaddrsize;
  char serverip[ INET_ADDRSTRLEN ];

  if( argc == 2 )
	if( strcmp(argv[1], "-v") == 0 )
	  verbose = 1;
//initializing sockaddr_in structure, record structure
  record_init( &client_record );
  bzero( &servaddr, sizeof(servaddr) );
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl( INADDR_ANY );
  servaddr.sin_port = htons(SERV_PORT);
//activating server: create socket, bind to address:port, set option, start listening
  listenfd = Socket( AF_INET, SOCK_STREAM, 0 );
  Bind( listenfd, (const SA*)&servaddr, sizeof(servaddr) );
  Setsockopt( listenfd, SOL_SOCKET, SO_REUSEADDR, ( char* )&on, sizeof( on ) );
  Listen( listenfd, BACKLOG );
  Getsockname( listenfd, (SA*)&servaddr, &servaddrsize );
//initializing fd_set structure, maxfd
  bzero( &rset, sizeof(rset) );
  bzero( &rset_new, sizeof(rset_new) );
  FD_SET( listenfd, &rset_new );
  maxfd = listenfd + 1;
//spining for incomming message or connection
  while(1){
    rset = rset_new;
    client_ready = Select( maxfd, &rset, NULL, NULL, NULL );
    //new connection request
    if( FD_ISSET( listenfd, &rset ) ){
      cliaddrsize = sizeof(cliaddr);
      connfd = Accept( listenfd, (SA*)&cliaddr, &cliaddrsize );

      //notice online clients
      snprintf( buf, sizeof(buf), "/serv Someone is coming.\n" );
      broadcast( buf, strlen(buf), &client_record );
      
      //add new client into record
      record_add( connfd, &cliaddr, &client_record );
      FD_SET( connfd, &rset_new );
      //adjust upper bound for rset_new
      if( connfd >= maxfd )
        maxfd = connfd + 1;

      //greets the new client
	  read( connfd, serverip, INET_ADDRSTRLEN+1 );
      snprintf( buf, sizeof(buf), "/serv Hello, anonymous! ServerIP: %s:%d\n", serverip, SERV_PORT );
      Writen( connfd, buf, strlen(buf) );

    }

    client_record_size = client_record.size;
    for( i = 0 ; i < client_record_size; i++ ){
      if( (connfd = client_record.fd[i]) >= 0 )
        if( FD_ISSET( connfd, &rset ) ){
          if( (bytes_read = Read( connfd, buf, MAXLINE )) > 0 )
            server_react( buf, bytes_read, i, &client_record );
          else if( bytes_read < 0 )
            err_sys( "str_echo: read error" );
          else if( bytes_read == 0 ){
            if( verbose == 1 )
			  printf( "closing connection %d\n", connfd );
			on = record_findbyfd(connfd, &client_record);
            snprintf( buf, sizeof(buf), "/serv %s is offline.\n", client_record.name[on] );
			broadcast_noback( buf, strlen(buf), on, &client_record );
            close( connfd );
            record_delete( i, &client_record);
            FD_CLR( connfd, &rset_new );
          }
          if( --client_ready <= 0 )
            break;
        }
    }
  }
  return 0;
}

void server_react( char* buf, int bytes_read, int src, struct record* ptr_record )
{
  int i, n, connfd, client_record_size;
  char username[NAMESIZE];
  char msg[MAXLINE];
  char* name;

  connfd = ptr_record->fd[src];
  //broadcast
  if( buf[0] != '/' ){
    if( verbose == 1 )
	  printf( "%d issued a broadcast\n", ptr_record->fd[src] );
    buf[bytes_read] = '\0';
    snprintf( msg, sizeof(msg), "/msg %s SAID: %s", ptr_record->name[src], buf );
    broadcast( msg, strlen(msg), ptr_record );
  } else if( strcmp( buf, "/who\n" ) == 0 ){
    if( verbose == 1 )
	  printf( "%d issued a command /who\n", connfd );
    client_record_size = ptr_record->size;
    for( i = 0 ; i < client_record_size; i++ ) {
      if( ptr_record->fd[i] >= 0 ) {
        snprintf( msg, sizeof(msg), "/serv %s %s:%d\n", ptr_record->name[i], ptr_record->addrstr[i], ptr_record->port[i] );
        Writen( connfd, msg, strlen(msg) ); 
      }
    }
  } else if( strncmp( buf, "/nick ", 6 ) == 0 ) {
    if( verbose == 1 )
	  printf( "%d issued a command /nick\n", connfd );
    name = convert_name( buf, src, ptr_record );
    if( name == NULL ){
      if( errno == ENAMEANON )
        strcpy( msg, "/serv ERROR: Username can not be anonymous.\n" );
      else if( errno == ENAMEFORMAT )
        strcpy( msg, "/serv ERROR: Username can only consists of 2~12 English letters.\n" );
      else if( errno == ENAMEUSED )
        snprintf( msg, sizeof(msg), "/serv ERROR: %s has been used by others.\n", buf+6 );

      Writen( connfd, msg, strlen(msg) );
    } else{
      snprintf( msg, sizeof(msg), "/serv %s is now known as %s\n", ptr_record->name[src], name );
      strcpy( ptr_record->name[src], name );
      broadcast_noback( msg, strlen(msg), src, ptr_record );
      snprintf( msg, sizeof(msg), "/serv You're now known as %s\n", ptr_record->name[src] );
      Writen( connfd, msg, strlen(msg) );
    }
  } else if( strncmp( buf, "/private ", 9 ) == 0 ) {
    if( verbose == 1 )
	  printf( "%d issued a command /private\n", connfd );
    i = record_findbyfd( connfd, ptr_record );
    if( strcmp( ptr_record->name[i], "anonymous" ) == 0 ){
      strcpy( msg, "/serv ERROR: You are anonymous.\n" );
      Writen( connfd, msg, strlen(msg) );
    } else{
      strtok( buf, " " );
      name = strtok( NULL, " " );
      n = record_findbyname( name, ptr_record );
      if( n < 0 ){
        if( errno == ETOANON )
          strcpy( msg, "/serv ERROR: The client to which you sent is anonymous.\n" );
        else if( errno == ENOUSER )
          strcpy( msg, "/serv ERROR: The receiver doesn't exist.\n" );
        Writen( connfd, msg, strlen(msg) );
      } else{
        if( verbose == 1 )
		  printf( "    from %s to %s\n", ptr_record->name[i], ptr_record->name[n] );
        buf[bytes_read] = '\0';
        snprintf( msg, sizeof(msg), "/private %s %s\n\0", ptr_record->name[i], strtok(NULL, "\n") );
        Writen( ptr_record->fd[n], msg, strlen(msg) );
        strcpy( msg, "/serv SUCCESS: Your message has been sent.\n" );
        Writen( connfd, msg, strlen(msg) );
      }
    }
  } else{
    strcpy( msg, "/serv ERROR: Error command\n" );
	Writen( connfd, msg, strlen(msg) );
  }
    
}

//convert_name convert the /nick new_name command into new_name
//return NULL and set errno if the name is not convertabal
//return pointer to the legal name if convertion is successful
char* convert_name( char* buf, int src, struct record* ptr_record )
{
  int i, n, client_record_size;
  
  char* name;
  name = strtok( buf, " " );
  //take off /nick and replace '\n' with '\0'
  name = strtok( NULL, "\n" );

  if( strcmp(name, "anonymous" ) == 0 ){
    errno = ENAMEANON;
    return NULL;
  }
  n = strlen( name );
  if( n<2 || n>12 ){
    errno = ENAMEFORMAT;
    return NULL;
  }
  for( i = 0 ; i < n ; i++ )
    if( !isalpha( name[i] ) ){
      errno = ENAMEFORMAT;
      return NULL;
    }
  client_record_size = ptr_record->size;
  for( i = 0 ; i < client_record_size; i++ )
    if( (i!=src) && (ptr_record->fd[i] >= 0) && (strcmp( name, ptr_record->name[i]) == 0) ){
      errno = ENAMEUSED;
      return NULL;
    }

  return name;
}

void broadcast( const char* msg, size_t n, struct record* ptr_record )
{
  int i, connfd, client_record_size = ptr_record->size;
  for( i = 0 ; i < client_record_size; i++ )
    if( (connfd = ptr_record->fd[i]) >= 0 )
      Writen( connfd, msg, n ); 
}

void broadcast_noback( const char* msg, size_t n, int src, struct record* ptr_record )
{
  int i, connfd, client_record_size = ptr_record->size;
  for( i = 0 ; i < client_record_size; i++ )
    if( (i!=src) && ((connfd = ptr_record->fd[i]) >= 0) )
      Writen( connfd, msg, n ); 
}

void record_init( struct record* ptr_record )
{
  int i;
  for( i = 0 ;i < FD_SETSIZE ; i++ ){
    ptr_record->fd[i] = -1;
  }
}

int record_exist( int i, struct record* ptr_record )
{
  return (ptr_record->fd[i] < 0)? 0 : 1;
}

//find name will return the index into client_record.name if
//1. name != anonymous. 2. an online user has the name name
//otherwise return -1
int record_findbyname( const char* name, struct record* ptr_record )
{
  if( strcmp(name, "anonymous") == 0 ){
    errno = ETOANON;
    return -1;
  }

  int i, client_record_size = ptr_record->size;
  for( i = 0 ; i < client_record_size; i++ )
    if( ((ptr_record->fd[i])>=0) && (strcmp( ptr_record->name[i], name )==0) )
      return i;

  errno = ENOUSER;
  return -1;
}

//find name will return the index into client_record.fd if
//the fd is in on of the records
//otherwise return -1
int record_findbyfd( int fd, struct record* ptr_record )
{
  int i, client_record_size = ptr_record->size;
  for( i = 0 ; i < client_record_size; i++ )
    if( ptr_record->fd[i] == fd )
      return i;
  
  return -1;
}

int record_add( int in_fd, struct sockaddr_in* in_addr, struct record* ptr_record )
{
  int i;
  for( i = 0 ; (i < FD_SETSIZE) && (ptr_record->fd[i] >= 0) ; i++ ) ;
  if( i == FD_SETSIZE )
    err_sys( "record_add: too many clients" );
  if( i == ptr_record->size )
    ptr_record->size++;

  ptr_record->fd[i] = in_fd;
  Inet_ntop( AF_INET, &in_addr->sin_addr, ptr_record->addrstr[i], INET_ADDRSTRLEN );
  ptr_record->port[i] = ntohs(in_addr->sin_port);
  strcpy( ptr_record->name[i], "anonymous" );

  if( verbose == 1 )
	printf( "client %d from %s has been added, the client_record size is now: %d\n", in_fd, ptr_record->addrstr[i], ptr_record->size );

  return i;
}

void record_delete( int i, struct record* ptr_record )
{
  ptr_record->fd[i] = -1;
}
