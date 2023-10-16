// Trivial Torrent

// https://pubs.opengroup.org/onlinepubs/9699919799

// TODO: some includes here

#include "file_io.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>

// TODO: hey!? what is this?

/**
 * This is the magic number (already stored in network byte order).
 * See https://en.wikipedia.org/wiki/Magic_number_(programming)#In_protocols
 */
static const uint32_t MAGIC_NUMBER = 0xde1c3232; // = htonl(0x32321cde);

static const uint8_t MSG_REQUEST = 0;
static const uint8_t MSG_RESPONSE_OK = 1;
static const uint8_t MSG_RESPONSE_NA = 2;


static const uint16_t BUFFER_SIZE = 13;

struct message_t {
	uint32_t magic_number;
	uint8_t message_code;
	uint64_t block_number;
};

//COMUNAL FUNCTIONS
int correct_enter(int argc, char** argv);
struct torrent_t metadata_downloading(const int argc, char **argv, int *error_code);
char* get_new_metadata_name(const char* name_metadata_file);

//CLIENT FUNCTIONS
int request_block(const uint64_t block_i, const int peer_server_socket);
int store_data_to_file(const uint64_t position, struct block_t rec_block,  struct torrent_t metadata);
struct block_t recieve_from_server(const uint64_t position, const struct torrent_t metadata, const int peer_server_socket);

//SERVER FUNCTIONS
int create_server_socket(const uint16_t port);
int accept_connection(const int server_socket);
int connect_to_client(const int ssocket);
int main_server_loop(const int cl_socket, const struct torrent_t metadata);


/////////////////////////// COMUNAL FUNCTIONS ///////////////////////////////

/**
 * This function checks if the number of arguments is correct and if the document extension is correct too.
 * If argc is 2 the comprovations are for client mode but if argc is 4 the comprovations are for server mode.
 * 
 * @param argc Lenght of the arguments of the command line
 * @param argv Array of arguments from de command line
 * @return 0 All comprovations of Client mode are correct and indicate that initialize on Client mode.
 * @return 1 All comprovations of Server mode are correct and indicate that initialize on Server mode.
 * @return 2 There is an error in the arguments.
 */

int correct_enter(int argc, char** argv){
	int mode = 2;
	if (argc == 2){ //Comprovations for Client mode

		char* idownloaded = strchr(argv[1], '.'); // Caching extension
		if (idownloaded == NULL)
			log_printf(LOG_INFO, "Error: No metadata file");
		else{
			if (strcmp(idownloaded,".ttorrent") == 0) // comparing if the extension is correct
				mode = 0;
			else
				log_printf(LOG_INFO, "Error: Metadata file is not a .ttorrent file");
		}
	} else if(argc == 4){ // Comprovations for Server mode

		if(strcmp(argv[1],"-l") == 0){
			if(strcmp(argv[2],"8080") == 0 || strcmp(argv[2],"8081") == 0 || strcmp(argv[2],"8082") == 0){
				char* idownloaded = strchr(argv[3], '.'); // Caching extension
				if (idownloaded == NULL)
					log_printf(LOG_INFO, "Error: No metadata file");
				else{
					if (strcmp(idownloaded,".ttorrent") == 0) // comparing if the extension is correct
						mode = 1;
					else
						log_printf(LOG_INFO, "Error: Metadata file is not a .ttorrent file");
				}
			} else
				log_printf(LOG_INFO, "Error: Port is not valid");
		} else 
			log_printf(LOG_INFO, "Server mode: No -l option specified");
	}

	return mode;
}
/**
 * This function downloads the metadata file and return a torrent struct for server and client.
 *
 * @param argc number of arguments from de command line
 * @param argv array of arguments from de command line
 * @param error_code pointer to an integer that will contain the error code
 * @return a torrent struct with the metadata information
 */
struct torrent_t metadata_downloading(const int argc,  char **argv, int *error_code) {
	struct torrent_t torrent;
	*error_code = 0;
	if (argc <= 2)
	{
		char* data = argv[1];
		char* download_file = get_new_metadata_name(data);
		log_printf(LOG_INFO, "Download filename");

		//Cheking de la creacion del archivo metainfo
		int ctfmf = create_torrent_from_metainfo_file(data, (struct torrent_t*)&torrent, download_file);
		log_printf(LOG_INFO, "created torrent");
		if (ctfmf == -1) {
			perror("Metainfo file could not be created");
			*error_code = 1;
		}
	} else{
		char* data = argv[3];
		char* download_file = get_new_metadata_name(data);
		log_printf(LOG_INFO, "Download filename");

		//Cheking de la creacion del archivo metainfo
		int ctfmf = create_torrent_from_metainfo_file(data, (struct torrent_t*)&torrent, download_file);
		log_printf(LOG_INFO, "created torrent");
		if (ctfmf == -1) {
			log_printf(LOG_INFO, "Metainfo file could not be created");
			*error_code = 1;
		}
	}
	return torrent;
}
/**
 * This function creates a new name for the metadata file erasing the extension of the ttorrent file.
 *
 * @param name_metadata_file name of the ttorrent file
 * @return a new name for the metadata file
 */
char* get_new_metadata_name(const char* name_metadata_file) {

	char* downloaded = {0};
	char* idownloaded = strchr(name_metadata_file, '.');
	if (idownloaded){
		size_t i = (size_t)idownloaded - (size_t)name_metadata_file;
		downloaded = malloc(i + 1);
		memcpy(downloaded, name_metadata_file, i);
		downloaded[i] = '\0';

	}
	
	log_printf(LOG_INFO,"New name for metadata file: %s", downloaded);

	return downloaded;
}

/////////////////////////// SERVER FUNCTIONS ///////////////////////////////

/**
 * This function creates a server socket and waits for a client to connect.
 * 
 * @param port port to listen
 * @return the server socket
 */
int create_server_socket(const uint16_t port){

	log_printf(LOG_INFO, "Starting server...");

	int srv_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (srv_socket == -1) {
		log_printf(LOG_INFO, "Error creating server socket");
		return 1;
	}
	log_printf(LOG_INFO, "Server socket created");

	//Create the server address of the server
	struct sockaddr_in srv_addr;
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(port);
	srv_addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(srv_socket, (struct sockaddr *) &srv_addr, sizeof(srv_addr)) == -1) { // bind the socket to the server address
		log_printf(LOG_INFO, "Error binding server socket");
		return 1;
	}
	log_printf(LOG_INFO, "Server socket binded");

	if (listen(srv_socket, 5) == -1) { // listen for connections
		log_printf(LOG_INFO, "Error listening server socket");
		return 1;
	}
	log_printf(LOG_INFO, "Server socket listening");

	return srv_socket;
}

/**
 * This function accepts a connection from a client and returns the client socket.
 * 
 * @param ssocket server socket
 * @return the client socket
 * @return -1 if an error occurs
 */
int connect_to_client(const int ssocket) {
	struct sockaddr_in cl_addr;
	socklen_t cl_addr_len = sizeof(cl_addr);
	int cl_connect = accept(ssocket, (struct sockaddr *) &cl_addr, &cl_addr_len); // accept the connection
	if (cl_connect == -1) {
		log_printf(LOG_INFO, "Error accepting client socket");
		close(ssocket);
		return 1;
	}
	log_printf(LOG_INFO, "Connection with client established");

	return cl_connect;
}

/**
 * This function is the main loop of the server.
 * 
 * @param cl_socket Client socket
 * @param metadata torrent metadata structure
 * 
 * @return 0 if everything is ok
 * @return 1 if an error occurs
 */

int main_server_loop(const int cl_socket,const struct torrent_t metadata){

    struct block_t block;
	uint8_t buffer[BUFFER_SIZE];
	int returner = 0;

	log_printf(LOG_INFO, "Starting server loop...");
	log_printf(LOG_INFO, "\t %d", metadata.block_count);

	for (int i = 0; i < (int) metadata.block_count; i++) {
		const ssize_t received_bytes = recv (cl_socket, buffer, sizeof(buffer), 0); // receive the block from the client
		/**
		* To know which variables to use them to decode tme message
		* buffer[0:3] = magic number
		* buffer[4] = MSG_REQUEST
		* buffer[5:12] = block_i
		**/

		//Decoding the message
		uint32_t magic_number_request = ((uint32_t) buffer[3] << 24) | ((uint32_t) buffer[2] << 16) |
			((uint32_t) buffer[1] << 8) | (uint32_t) buffer[0];
		uint64_t requested_block_id = ((uint64_t) buffer[5] << 56) | ((uint64_t) buffer[6] << 48) |
			((uint64_t) buffer[7] << 40) | ((uint64_t) buffer[8] << 32) | ((uint64_t) buffer[9] << 24) | 
			((uint64_t) buffer[10] << 16 ) | ((uint64_t) buffer[11] << 8) | (uint32_t) buffer[12];
		
		log_printf(LOG_INFO, "\tBlock_id: %d", requested_block_id);
		
		if (received_bytes < BUFFER_SIZE || requested_block_id > INT_MAX || magic_number_request != MAGIC_NUMBER){

			if (metadata.block_map[requested_block_id]){ // Checking if the block really exist
					log_printf(LOG_INFO, "Block at position %d does exist", (int) requested_block_id);
					if (load_block(&metadata, requested_block_id, &block) == -1) // Loading the block
						log_printf(LOG_INFO, "Error loading block");
					log_printf(LOG_INFO, "Block at position %d is valid", (int) requested_block_id);
					buffer[4] = MSG_RESPONSE_OK; // Changing the controler status to MSG_RESPONSE_OK
			}
			returner = 0;
		} else {
			buffer[4] = MSG_RESPONSE_NA; // Changing the controler status to MSG_RESPONSE_NA
		}
		log_printf(LOG_INFO, "\tInformacion en el bloque: %d", block.data);

		if ( send(cl_socket, buffer, sizeof(buffer), 0) == -1){ //Sending headers
			log_printf(LOG_INFO, "Error sending headers");
			returner = 1;
			break;
		}

		if ( send(cl_socket, block.data, block.size, 0) == -1){ //Sending data
			log_printf(LOG_INFO, "Error sending data");
			returner = 1;
			break;
		}
	} 

	return returner;
}

////////////////////////////// CLIENT FUNCTIONS //////////////////////////////////

/**
 * This function sends a request of the block to a server peer.
 * 
 * @param block_i block id to request
 * @param peer_server_socket peer socket to send the request
 * @return 0 if everything is ok
 * @return 1 if an error occurs
 */

int request_block(const uint64_t block_i, const int peer_server_socket) {

	uint8_t buffer[BUFFER_SIZE];

	//Encoding the message to send
	buffer[0] = (uint8_t)(MAGIC_NUMBER >> 24) & 0xff;
	buffer[1] = (uint8_t)(MAGIC_NUMBER >> 16) & 0xff;
	buffer[2] = (uint8_t)(MAGIC_NUMBER >> 8) & 0xff;
	buffer[3] = (uint8_t)(MAGIC_NUMBER >> 0) & 0xff;
	buffer[4] = (uint8_t)MSG_REQUEST;
	buffer[5] = (uint8_t)(block_i >> 56) & 0xff;
	buffer[6] = (uint8_t)(block_i >> 48) & 0xff;
	buffer[7] = (uint8_t)(block_i >> 40) & 0xff;
	buffer[8] = (uint8_t)(block_i >> 32) & 0xff;
	buffer[9] = (uint8_t)(block_i >> 24) & 0xff;
	buffer[10] = (uint8_t)(block_i >> 16) & 0xff;
	buffer[11] = (uint8_t)(block_i >> 8) & 0xff;
	buffer[12] = (uint8_t)(block_i >> 0) & 0xff;

	ssize_t sent_bytes = send(peer_server_socket, buffer, sizeof(buffer), 0); // Sending the request

	if (sent_bytes < 0)
		return 1;

	log_printf(LOG_INFO, "\t%d bytes sent to peer", sent_bytes);

	log_message(LOG_INFO, "\tWaiting for response");

	return 0;
}

/**
 * This function receives data response header from a server peer.
 * 
 * @param peer_server_socket peer socket to receive the response
 * @param metadata torrent metadata structure
 * @param position position of the block in the metadata
 * 
 * @return 0 if everything is ok
 * @return 1 if an error occurs
 */

struct block_t recieve_from_server(const uint64_t position, const struct torrent_t metadata, const int peer_server_socket){

	uint8_t header_buffer_response[BUFFER_SIZE];
	struct block_t rec_block;

	ssize_t received_header_bytes = recv(peer_server_socket, header_buffer_response, sizeof(header_buffer_response), 0); // Reciving headers from server
	log_printf(LOG_INFO, "\t%d bytes recieved to peer", received_header_bytes);
	log_printf(LOG_INFO, "\t %s", header_buffer_response);

	if (received_header_bytes < 0)
		return rec_block;
	
	if ((uint8_t) header_buffer_response[4] == MSG_RESPONSE_NA) {
		log_message(LOG_INFO, "\tServer peer does not have the block!");
		return rec_block;
	}


	uint64_t signed_block_lenght = get_block_size(&metadata, position);
	size_t receiving_block_size = (size_t) signed_block_lenght;

	log_printf(LOG_INFO,"\tSize of requested block: %d", receiving_block_size);

	uint8_t * receive_buffer = malloc(receiving_block_size);
	ssize_t received_bytes = 0;
	size_t total_bytes = 0;

	do { //Reciving the correct block data from the server
		received_bytes += recv ( peer_server_socket, receive_buffer + received_bytes, receiving_block_size - (size_t) received_bytes, 0);
		log_printf(LOG_INFO,"\tReceiving %d bytes of payload", received_bytes);
		total_bytes += (size_t) received_bytes;
	} while (total_bytes < receiving_block_size);

	if (received_bytes < 0 || (size_t) received_bytes != receiving_block_size){ //Checking if there was any error receiving the block before storing.
		perror("recv: ");
		return rec_block;
	}

	rec_block.size = (uint64_t) received_bytes;

	log_printf(LOG_INFO,"\tReading %d bytes of payload", receiving_block_size);

	for (uint64_t i = 0; i < rec_block.size; i++){
		rec_block.data[i] = receive_buffer[i];
	}

	free(receive_buffer);

	return rec_block;
}

/**
 * This function store all data in the torrent struct.
 * 
 * @param position position ofthe block to store
 * @param metadata torrent metadata structure
 * @param rec_block block to store
 * 
 * @return 0 if everything is ok
 * @return 1 if an error occurs
 */

int store_data_to_file(const uint64_t position, struct block_t rec_block, struct torrent_t metadata){

	log_message(LOG_INFO,"\tStoring block");

	int store_code = store_block(&metadata, position, &rec_block);

	if (store_code < 0)
		return 1;
	else {
		log_message(LOG_INFO,"\tBlock was stored succesfully!");
		assert(metadata.block_map[position]);
	}

	return 0;
}

/**
 * Main function.
 */
int main(int argc, char **argv) {

	set_log_level(LOG_DEBUG);

	log_printf(LOG_INFO, "Trivial Torrent (build %s %s) by %s", __DATE__, __TIME__, "Gabriel Juan and Ruben Martinez");

	// ==========================================================================
	// Parse command line
	// ==========================================================================

	// TODO: some magical lines of code here that call other functions and do various stuff.

	int enter = correct_enter(argc, argv);
	
	if (enter == 0 || enter == 1) {//comprovations if all arguments of argv and the number of argc are correct.
		struct torrent_t metadata;
		int error_download;
		metadata = metadata_downloading(argc, argv, &error_download); //creating the metadata structure and file.
		if (error_download == 1){
			log_printf(LOG_INFO, "Metainfo file could not be downloaded");
			return 1;
		}
		log_printf(LOG_INFO, "Metainfo file downloaded succesfully");
	
		if (enter == 0) { //Client mode

			log_message(LOG_INFO, "Client mode");
			
			uint32_t direccion = 0;		

			//Checking if blocks are correct
			uint64_t vblocks_counter = 0;

			for (uint64_t i = 0; i < metadata.block_count; i++){ //Look over the number of blocksof the downloaded file
				log_printf(LOG_INFO, "iterating through blocks");
				if (metadata.block_map[i] == 0) {
					// Array of integers that indicate if a block is correct or not
					log_printf(LOG_INFO, "if condition");
					vblocks_counter++;
				}
			}
			log_printf(LOG_INFO, "Number of valid blocks: %" PRId64 "\n", vblocks_counter);

			if (vblocks_counter <= metadata.block_count){
				//Conection to server for each peer
				for (uint64_t peer_i = 0; peer_i < metadata.peer_count && vblocks_counter <= metadata.block_count; peer_i++) { //Numero de peers disponbles
					const int client_socket = socket(AF_INET, SOCK_STREAM, 0);
					if (client_socket == -1)
						return 1;

					if (sizeof(metadata.peers[peer_i].peer_address) == -1) { //Array with peer addresses, extratc the address for each-one

						log_printf(LOG_INFO,"Server peer address is invalid");
						return -1;
					}

					//Creation of the sockaddr_in structure
					struct sockaddr_in addr;
					addr.sin_family = AF_INET;
					addr.sin_port = metadata.peers[peer_i].peer_port;
					addr.sin_addr.s_addr = htonl(direccion);

					socklen_t len = sizeof(struct sockaddr_in);
					//connection to the server
					int socket_connection = connect(client_socket, (struct sockaddr*) &addr, len);

					if (socket_connection == -1) {
						perror("Connection could not be established");
					} else
						log_printf(LOG_INFO, "Connected succesfully to peer");

					//For each incorrect block, we send a request to the server
					for (uint64_t block_i = 0; block_i < metadata.block_count && socket_connection == 0; block_i++) { //Recorremos el numero de bloques del archivo descargado

						if (metadata.block_map[block_i] == 0) { // Array of integers that indicateif the block is correct or not

							int req_block = request_block(block_i, client_socket);//Sending a request to server and we recieve data headers
							if (req_block == 1)
								return MSG_RESPONSE_NA;
							struct block_t recv_block = recieve_from_server((uint64_t)block_i, metadata, client_socket);//Sending a request to server and we recieve the correct blocks
							
							int store = store_data_to_file((uint64_t)block_i, recv_block, metadata);//Store the data into the downloaded file
							if (store == 1)
								return 1;
						}
					}
					// Close socket
					int fin = close(client_socket);
					if (fin == -1)
						return 1;

					if (socket_connection == 0) 
						break;
				}
			
			}

			destroy_torrent(&metadata);

		} else if (enter == 1){ //Server mode

			log_printf(LOG_INFO, "Server mode in port %s", argv[2]);
			int port = atoi(argv[2]); // This function transform a string(*char) to int
			
			int srv_socket = create_server_socket((uint16_t) port); // Create server socket
			if (srv_socket == 1){
				log_printf(LOG_INFO, "Server could not been started");
				return 1;
			}
			for (int i = 0; i < 3; i++){	
			//do{
				int client_s = connect_to_client(srv_socket); // Connect to client
				if (client_s == 1){
					log_printf(LOG_INFO, "Connection with client could not been established");
					close(srv_socket);
					return 1;
				}
				//Forking the server
				int control_forking = fork();
				log_printf(LOG_INFO, "Forking...\t Fork ID: %d", control_forking);
				if (control_forking > 0){
					main_server_loop(client_s, metadata); // Server main loop
				} else if (control_forking == 0){ //Parent process
					close(client_s);
					/*log_printf(LOG_INFO, "father connect");
					int client_sf = connect_to_client(srv_socket);
						if (client_sf == -1){
							log_printf(LOG_INFO, "Connection with client could not been established");*/
				
				//	listen(srv_socket, 1);
				} else if (control_forking == -1){ //Error on forking the server
					close(client_s);
					log_printf(LOG_INFO, "Forking error");
			//		break;
				}

			}//while();
			log_printf(LOG_INFO, "Closing server socket");
			close(srv_socket);

			log_printf(LOG_INFO, "Destroying metadata structure");
			destroy_torrent(&metadata);
			
		}
	} else {
		log_printf(LOG_INFO, "Not correct arguments for exectutate this program");
		return 1;
	}

	// The following statements most certainly will need to be deleted at some point...
	(void) argc;
	(void) argv;
	(void) MAGIC_NUMBER;
	(void) MSG_REQUEST;
	(void) MSG_RESPONSE_NA;
	(void) MSG_RESPONSE_OK;

	return 0;
}
