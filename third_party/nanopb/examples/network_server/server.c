/* This is a simple TCP server that listens on port 1234 and provides lists
 * of files to clients, using a protocol defined in file_server.proto.
 *
 * It directly deserializes and serializes messages from network, minimizing
 * memory use.
 * 
 * For flexibility, this example is implemented using posix api.
 * In a real embedded system you would typically use some other kind of
 * a communication and filesystem layer.
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include <pb_encode.h>
#include <pb_decode.h>

#include "fileproto.pb.h"
#include "common.h"

/* This callback function will be called once during the encoding.
 * It will write out any number of FileInfo entries, without consuming unnecessary memory.
 * This is accomplished by fetching the filenames one at a time and encoding them
 * immediately.
 */
bool listdir_callback(pb_ostream_t *stream, const pb_field_t *field, void * const *arg)
{
    DIR *dir = (DIR*) *arg;
    struct dirent *file;
    FileInfo fileinfo = {};
    
    while ((file = readdir(dir)) != NULL)
    {
        fileinfo.inode = file->d_ino;
        strncpy(fileinfo.name, file->d_name, sizeof(fileinfo.name));
        fileinfo.name[sizeof(fileinfo.name) - 1] = '\0';
        
        /* This encodes the header for the field, based on the constant info
         * from pb_field_t. */
        if (!pb_encode_tag_for_field(stream, field))
            return false;
        
        /* This encodes the data for the field, based on our FileInfo structure. */
        if (!pb_encode_submessage(stream, FileInfo_fields, &fileinfo))
            return false;
    }
    
    return true;
}

/* Handle one arriving client connection.
 * Clients are expected to send a ListFilesRequest, terminated by a '0'.
 * Server will respond with a ListFilesResponse message.
 */
void handle_connection(int connfd)
{
    DIR *directory = NULL;
    
    /* Decode the message from the client and open the requested directory. */
    {
        ListFilesRequest request = {};
        pb_istream_t input = pb_istream_from_socket(connfd);
        
        if (!pb_decode(&input, ListFilesRequest_fields, &request))
        {
            printf("Decode failed: %s\n", PB_GET_ERROR(&input));
            return;
        }
        
        directory = opendir(request.path);
        printf("Listing directory: %s\n", request.path);
    }
    
    /* List the files in the directory and transmit the response to client */
    {
        ListFilesResponse response = {};
        pb_ostream_t output = pb_ostream_from_socket(connfd);
        
        if (directory == NULL)
        {
            perror("opendir");
            
            /* Directory was not found, transmit error status */
            response.has_path_error = true;
            response.path_error = true;
            response.file.funcs.encode = NULL;
        }
        else
        {
            /* Directory was found, transmit filenames */
            response.has_path_error = false;
            response.file.funcs.encode = &listdir_callback;
            response.file.arg = directory;
        }
        
        if (!pb_encode(&output, ListFilesResponse_fields, &response))
        {
            printf("Encoding failed: %s\n", PB_GET_ERROR(&output));
        }
    }
    
    if (directory != NULL)
        closedir(directory);
}

int main(int argc, char **argv)
{
    int listenfd, connfd;
    struct sockaddr_in servaddr;
    int reuse = 1;
    
    /* Listen on localhost:1234 for TCP connections */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    servaddr.sin_port = htons(1234);
    if (bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0)
    {
        perror("bind");
        return 1;
    }
    
    if (listen(listenfd, 5) != 0)
    {
        perror("listen");
        return 1;
    }
    
    for(;;)
    {
        /* Wait for a client */
        connfd = accept(listenfd, NULL, NULL);
        
        if (connfd < 0)
        {
            perror("accept");
            return 1;
        }
        
        printf("Got connection.\n");
        
        handle_connection(connfd);
        
        printf("Closing connection.\n");
        
        close(connfd);
    }
    
    return 0;
}
