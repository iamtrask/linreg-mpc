
#define _GNU_SOURCE // for asprintf

#include <gcrypt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <zmq.h>

#include "error.h"
#include "fixed.h"
#include "test/test_multiplication.pb-c.h"


const int precision = 10;
const char *endpoint[3] = {
	"tcp://127.0.0.1:4567",
	"tcp://127.0.0.1:4568",
	"tcp://127.0.0.1:4569"
};

static int receive_value(fixed32_t *value, void *sender) {
	int status;
	check(value && sender, "receive_value: arguments may not be null");
	// receive message
	zmq_msg_t zmsg;
	status = zmq_msg_init(&zmsg);
	check(status == 0, "Could not initialize message: %s", zmq_strerror(errno));
	int len = zmq_msg_recv(&zmsg, sender, 0);
	check(len != -1, "Could not receive message: %s", zmq_strerror(errno));
	// unpack message
	Test__Fixed *pmsg = test__fixed__unpack(NULL, len, zmq_msg_data(&zmsg));
	check(pmsg != NULL, "Could not unpack message");
	*value = pmsg->value;

	// cleanup
	test__fixed__free_unpacked(pmsg, NULL);
	zmq_msg_close(&zmsg);
	return 0;

error:
	return -1;
}

static int send_value(fixed32_t value, void *recipient) {
	int status;
	check(recipient, "send_value: recipient may not be null");
	// initialize message
	Test__Fixed pmsg;
	test__fixed__init(&pmsg);
	pmsg.value = value;

	zmq_msg_t zmsg;
	status = zmq_msg_init_size(&zmsg, test__fixed__get_packed_size(&pmsg));
	check(status == 0, "Could not initialize message: %s", zmq_strerror(errno));
	// pack payload and send
	test__fixed__pack(&pmsg, zmq_msg_data(&zmsg));
	status = zmq_msg_send(&zmsg, recipient, ZMQ_DONTWAIT); // asynchronous send
	check(status != -1, "Could not send message: %s", zmq_strerror(errno));

	// cleanup
	zmq_msg_close(&zmsg);
	return 0;
error:
	return -1;
}

int main(int argc, char **argv) {
	void *socket[3] = {NULL, NULL, NULL};
	void *context = NULL;
	int status, retval = 1;

	check(argc >= 2, "Usage: %s Party [Input]", argv[0]);

	// parties are indexed starting with 1 to be 
	// consistent with notation inpapers
	int party = 0;
	if(!strcmp(argv[1], "1")) {
		party = 1;
	} else if(!strcmp(argv[1], "2")) {
		party = 2;
	} else if(!strcmp(argv[1], "3")) {
		party = 3; // the trusted initializer (TI)
	}
	check(party > 0, "Party must be either 1, 2, or 3");

	// parties 1 and 2 provide inputs
	double input;
	if(party < 3) {
		check(argc >= 3, "Parties 1 and 2 need to provide an input");
		input = atof(argv[5]);
		check(errno == 0, "Invalid input: %s", strerror(errno));
	}

	// initialize zmq
	context = zmq_ctx_new();
	check(context, "Could not create context: %s", zmq_strerror(errno));
	// create and connect sockets
	for(int i = 0; i < 2; i++) {
		if(i == party - 1) { // create listen socket
			socket[i] = zmq_socket(context, ZMQ_REP);
			check(socket[i], "Could not create socket: %s", zmq_strerror(errno));
			status = zmq_bind(socket[i], endpoint[i]);
			check(status == 0, "Could not bind socket %s: %s", endpoint[i], zmq_strerror(errno));
		} else { // connect to the other parties
			socket[i] = zmq_socket(context, ZMQ_REQ);
			check(socket[i], "Could not create socket: %s", zmq_strerror(errno));
			status = zmq_connect(socket[i], endpoint[i]);
			check(status == 0, "Could not connect to %s: %s", endpoint[i], zmq_strerror(errno));
		}
		//int linger = 0; // discard unsent messages when shutting down
		//zmq_setsockopt(socket[i], ZMQ_LINGER, &linger, sizeof(linger));
	}
	
	if(party == 3) { // Trusted Initializer
		// initialize libgcrypt for RNG
		check(gcry_check_version(GCRYPT_VERSION), "Unsupported gcrypt version");
		gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
		// generate random masks and send them to parties 1 and 2
		for(int i = 0; i < 2; i++) {
			fixed32_t mask;
			gcry_randomize(&mask, 4, GCRY_STRONG_RANDOM);
			status = send_value(mask, socket[i]);
			check(status == 0, "Error sending message to party %d", i+1);
		}
	} else {
		fixed32_t a;
		fixed32_t x = double_to_fixed(input, precision);
		status = receive_value(&a, socket[party-1]);
		check(status == 0, "Error receiving message from TI");

		// send (x + a) to other party
		//status = send_value(x + a, socket[party % 2]);
		//check(status == 0, "Error sending message to party %d", (party % 2) + 1);

		// receive value from other party
		//fixed32_t a2;
		//status = receive_value(&a2, socket[party % 2]);
		//check(status == 0, "Error receiving message from party %d", (party % 2) + 1);
		

		printf("Value received: %x\n", a);
	}
		
	retval = 0;

error:
	for(int i = 0; i < 3; i++) {
		if(socket[i]) zmq_close(socket[i]);
	}
	if(context) zmq_ctx_destroy(context);
	return retval;
}
