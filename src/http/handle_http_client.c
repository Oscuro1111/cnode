#include "../../includes/error_exit.h"
#include "../../includes/http_error_codes.h"
#include "../../includes/http.h"
#include "../../includes/http_parser_utility.h"
#include "../../includes/utils.h"
#include "../../includes/fs_c.h"
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include <unistd.h>

void __response_error(Response *http_response, char *body, uint length, uint size)
{

	const uint32_t RES_SIZE = size * length + sizeof(Response) + 1024;

	String *string = allocate_string(0);

	fprintf(stderr, "\nRES_SIZE=%d\n", RES_SIZE);

	// Warning: NULL is not checked
	char *response = (char *)malloc(RES_SIZE);

	uint offset = 0;

	memset(response, 0, RES_SIZE);

	insert_string(&string, "HTTP/1.1 ");

	insert_string(&string, http_response->status);

	insert_string(&string, " ");

	insert_string(&string, "Error");

	offset +=
	    snprintf(response + offset, RES_SIZE - offset, "%s", string->str);

	response[offset++] = '\r';
	response[offset++] = '\n';

	free_string(string);

	if (strlen(http_response->content_length) > 0)
	{
		offset += snprintf(response + offset, RES_SIZE - offset, "%s: %s",
				   "Content-Length", http_response->content_length);
		response[offset++] = '\r';
		response[offset++] = '\n';
	}

	if (strlen(http_response->content_type) > 0)
	{
		offset += snprintf(response + offset, RES_SIZE - offset, "%s: %s",
				   "Content-Type", http_response->content_type);
		response[offset++] = '\r';
		response[offset++] = '\n';
	}

	if (strlen(http_response->accept_ranges) > 0)
	{

		offset += snprintf(response + offset, RES_SIZE - offset, "%s: %s",
				   "Accept-Ranges", http_response->accept_ranges);
		response[offset++] = '\r';
		response[offset++] = '\n';
	}

	if (strlen(http_response->connection) > 0)
	{
		offset += snprintf(response + offset, RES_SIZE - offset, "%s: %s",
				   "Connection", http_response->connection);
		response[offset++] = '\r';
		response[offset++] = '\n';
	}

	if (body != NULL)
	{
		response[offset++] = '\r';
		response[offset++] = '\n';

		offset += snprintf(response + offset, RES_SIZE - offset, "%s", body);
	}

	send(http_response->sockfd, response, offset, 0);
	free(response);
	fprintf(stderr, "Done response\n");
}

void response_error(Response *http_response, Request *request, char *message, const char *error_code)
{

	String *body = allocate_string(0);

	char len[16];

	insert_string(&body, message);

	insert_string(&body, " : ");

	insert_string(&body, "Error=");

	insert_string(&body, error_code);

	insert_string(&body, " request=");

	insert_string(&body, strlen(request->header.url_path) > 0 ? request->header.url_path : request->header.path - 1);

	int_to_str(len, body->current);

	set_responseHeader(http_response, CONTENT_TYPE, "text/plain");
	set_responseHeader(http_response, STATUS, error_code);
	set_responseHeader(http_response, CONTENT_LENGTH, len);
	set_responseHeader(http_response, ACCEPT_RANGES, "bytes");
	set_responseHeader(http_response, CONNECTION, "close");

	__response_error(http_response, body->str, body->current, sizeof(char));

	free_string(body);
}

int response_msg(int client_sock_fd, char *_msg)
{

	char *msg;

	String *string;

	char con_length[16];

	Response *http_response = (Response *)malloc(sizeof(Response));

	if (http_response == NULL)
	{
		return -1;
	}
	if (_msg == NULL)
	{
		_msg = "200 OK";
	}

	string = allocate_string(0);

	insert_string(&string, "<h2>");

	insert_string(&string, _msg);

	insert_string(&string, "</h2>");

	msg = string->str;

	int_to_str(con_length, strlen(msg));

	memset(http_response, 0, sizeof(Response));

	http_response->sockfd = client_sock_fd;

	set_responseHeader(http_response, CONTENT_TYPE, "text/html");
	set_responseHeader(http_response, STATUS, "200");

	set_responseHeader(http_response, CONTENT_LENGTH, con_length);
	set_responseHeader(http_response, ACCEPT_RANGES, "bytes");
	set_responseHeader(http_response, CONNECTION, "close");

	//sending response to client
	response(http_response, msg, strlen(msg), sizeof(char));

	//free resources
	free(http_response);
	free_string(string);

	return 0;
}

void post_request(Request *request, char *remain, uint32_t bytes_remain,
		  char *root)
{
	char *cache_file_path;
	uint32_t file_size;
	Queries *qrs = NULL;

	if (strcmp("multipart/form-data", request->header.content_type) == 0)
	{

		cache_file_path = recive(root, request, &file_size, remain, bytes_remain);

		parse_multipart_form(cache_file_path, root, request, file_size);

		//response_msg(request->clnt_sock,"Done uploading");
		free(cache_file_path);

		return;
	}

	if (strcmp("application/x-www-form-urlencoded",
		   request->header.content_type) == 0)
	{

		qrs = parse_encoded_url(request, remain, bytes_remain);

		request->header.body.form = qrs;

		return;
	}
}

int get_request(Request *request, char *resource_dir, char *remain,
		 uint32_t remain_bytes, char *root)
{

	char path[1024];
	char body[1024];

	char con_length[16];

	char *msg;

	FS_File fs_file = {
	    .is_alloc = 0,
	    .file = NULL,
	    .fd = -1};

	int length = 0;

	Response *http_response = (Response *)malloc(sizeof(Response));

	http_response->sockfd = request->clnt_sock;

	if (strcmp("favicon.ico", request->header.path) == 0)
	{
		response_msg(request->clnt_sock,"Not found");
		free(http_response);
		return -1;
	}

	if (strlen(request->header.path) == 0 ||
	    strcmp(request->header.path, "/") == 0)
	{

		fprintf(stderr, "--No file--");

		//path with name in reource directory 404
		fs_file.name = "error.html";

		//resource directory
		fs_file.path = resource_dir;
	}
	else
	{

		if (get_req_parser(request) == -1)
		{
			goto server_error;
		}

		//if route is called redirect to route handler
		if(request->header.route_name!=NULL){
			return 1;
		}


		//path with file name in resource directory
		fs_file.name = request->header.url_path + 1; // /path =>path
		//resource directory
		fs_file.path = resource_dir;
	}

	//Note:Route need to implement

	if (fs_read_file(&fs_file) == -1)
	{
	server_error:
		response_error(http_response, request, "Unable to process request!", HTTP_ERROR_CODES.CODE_503);
		free(http_response);
		return -1;
	}

	length = fs_file.size;

	int_to_str(con_length, length);

	memset(http_response, 0, sizeof(Response));

	http_response->sockfd = request->clnt_sock;

	set_responseHeader(http_response,CONTENT_TYPE,get_content_type(fs_file.name));
	set_responseHeader(http_response, STATUS, "200");
	set_responseHeader(http_response, CONTENT_LENGTH, con_length);
	set_responseHeader(http_response, ACCEPT_RANGES, "bytes");
	set_responseHeader(http_response, CONNECTION, "close");

	response(http_response, fs_file.buffer, length, sizeof(char));

	free(http_response);
	fs_close_file(&fs_file);

	return 0;
}

void free_request(Request *request)
{

	if (request->mem_buffer != NULL)
	{

		if (request->header.body.form != NULL)
		{
			{
				dispose_queries(request->header.body.form);
			}
		}
		if (request->get_req_mem != NULL)
		{
			if (request->header.qrs != NULL)
			{
				dispose_queries(request->header.qrs);
			}

			free(request->get_req_mem);
		}
		free(request->mem_buffer);
	}
}

void HandleClient(int clntSock, char *resource_dir, char *root,void (*routeHandler)(Request *))
{

	char remain[1024];
	int32_t remain_bytes;
        Response *http_response=NULL;

	//Must initalize with NULL for confirmation is they will be allocated or not and to escape garbage value
	Request request = {
	    .get_req_mem = NULL,
	    .header.qrs = NULL,
	    .mem_buffer = NULL,
	    .header.body.form = NULL,
	    .header.multipart_form.is_any_file = -1,
	};

	request.clnt_sock = clntSock;

	remain_bytes = recive_header(&request, remain, 1024);

	if(remain_bytes==-1){
		goto end_client;
	}

	if (strcmp(request.header.method, "GET") == 0)
	{
		if(get_request(&request, resource_dir, remain, remain_bytes, root)==1){
		         goto route_handler;	
		}else{
			goto end_client;
		}
	}
	else if (strcmp(request.header.method, "POST") == 0)
	{
		post_request(&request, remain, remain_bytes, root);
	}
	else
	{
		response_msg(request.clnt_sock, "Unsupported method by server!");
	}

route_handler:
	if(routeHandler!=NULL){
		routeHandler(&request);
	}else{

	        http_response = (Response *)malloc(sizeof(Response));
		if(http_response==NULL){
			fprintf(stderr,"Unable to allocate memory for respone struct.");
		}else{
		http_response->sockfd = (&request)->clnt_sock;
		response_error(http_response,&request, "Unable to process request!", HTTP_ERROR_CODES.CODE_404);
		free(http_response);		
	}     
		
}
end_client:
	free_request(&request);
}
