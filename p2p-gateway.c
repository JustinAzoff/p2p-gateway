//  --------------------------------------------------------------------------
//  Example Zyre distributed chat application

#include "zyre.h"

void
connect_to_certs_with_endpoints(zyre_t *zyre, zcertstore_t *certstore)
{
    zcert_t *cert;
    zlistx_t *certs = zcertstore_certs(certstore);
    cert = (zcert_t *) zlistx_first(certs);
    while (cert) {
        char *endpoint = zcert_meta (cert, "endpoint");
        char *real_endpoint = zsys_sprintf("%s|%s", endpoint, zcert_public_txt(cert));
        fprintf(stderr, "Connect to %s\n", real_endpoint);
        zyre_require_peer (zyre, endpoint, real_endpoint);
        zstr_free(&real_endpoint);
        cert = (zcert_t *) zlistx_next(certs);
    }

}

static void 
gateway_actor (zsock_t *pipe, void *args)
{
    const char *endpoint = getenv("ZYRE_BIND");
    const char *private_key_path = getenv("PRIVATE_KEY_PATH");
    const char *public_key_dir_path = getenv("PUBLIC_KEY_DIR_PATH");

    const char *pubsub_endpoint = getenv("PUBSUB_ENDPOINT");
    const char *control_endpoint = getenv("CONTROL_ENDPOINT");

    if(!endpoint) {
        fprintf(stderr, "export ZYRE_BIND=tcp://*:9200\n");
        exit(1);
    }

    if(!pubsub_endpoint)
        pubsub_endpoint = "tcp://127.0.0.1:14000";
    if(!control_endpoint)
        control_endpoint = "tcp://127.0.0.1:14001";
    if(!public_key_dir_path)
        public_key_dir_path = "./public_keys";

    zsock_t *pub = zsock_new(ZMQ_PUB);
    zsock_t *control = zsock_new(ZMQ_ROUTER);

    if (-1 == zsock_bind(pub, "%s", pubsub_endpoint)) {
        fprintf(stderr, "Faild to bind to PUBSUB_ENDPOINT %s", pubsub_endpoint);
        perror(" ");
        exit(1);
    }
    if (-1 == zsock_bind(control, "%s", control_endpoint)) {
        fprintf(stderr, "Faild to bind to CONTROL_ENDPOINT %s", control_endpoint);
        perror(" ");
        exit(1);
    }

    zcertstore_t *certstore = zcertstore_new(public_key_dir_path);

    zcert_t *cert = NULL;
    if(private_key_path) {
        cert = zcert_load(private_key_path);

        zactor_t *auth = zactor_new (zauth,NULL);
        zstr_send(auth,"VERBOSE");
        zsock_wait(auth);
        zstr_sendx (auth, "CURVE", public_key_dir_path, NULL);
        zsock_wait(auth);
    }

    zyre_t *node = zyre_new ((char *) args);
    if (!node)
        return;                 //  Could not create new node

    //FIXME: The order of the next few lines matters a lot for some reason
    //I should be able to start the node after the setup, but that isn't working
    //because self->inbox gets hosed somehow
    zyre_set_verbose (node);
    zyre_start (node);
    zclock_sleep(1000);
    if(cert) {
        zyre_set_curve_keypair(node, zcert_public_txt(cert), zcert_secret_txt(cert));
    }
    zyre_set_endpoint(node, "%s", endpoint);
    const char *uuid = zyre_uuid (node);
    printf("My uuid is %s\n", uuid);
    /*
    if(cert) {
        char *published_endpoint = zsys_sprintf("%s|%s", endpoint, zcert_public_txt(cert));
        zsimpledisco_publish(disco, published_endpoint, uuid);
    } else {
        zsimpledisco_publish(disco, endpoint, uuid);
    }
    */
    //zyre_join (node, "CHAT");
    zsock_signal (pipe, 0);     //  Signal "ready" to caller

    //zclock_sleep(1000);

    bool terminated = false;

    int64_t last_bootstrap = 0;

    zpoller_t *poller = zpoller_new (pipe, zyre_socket (node), control, NULL);
    while (!terminated) {
        void *which = zpoller_wait (poller, 2000);
        if (which == pipe) {
            zmsg_t *msg = zmsg_recv (which);
            if (!msg)
                break;              //  Interrupted

            char *command = zmsg_popstr (msg);
            if (streq (command, "$TERM"))
                terminated = true;
            else {
                puts ("E: invalid message to actor");
                assert (false);
            }
            free (command);
            zmsg_destroy (&msg);
        }
        else
        if (which == zyre_socket (node)) {
            zmsg_t *msg = zmsg_recv (which);
            char *event = zmsg_popstr (msg);
            char *peer = zmsg_popstr (msg);
            char *name = zmsg_popstr (msg);
            char *group = zmsg_popstr (msg);
            char *message = zmsg_popstr (msg);

            if (streq (event, "SHOUT")) {
                zsys_debug("zyre->pub %s: %s: %s", group, name, message);
                zstr_sendx (pub, group, name, message, NULL);
            }

            free (event);
            free (peer);
            free (name);
            free (group);
            free (message);
            zmsg_destroy (&msg);
        }
        /*
        else
        if (which == zsimpledisco_socket (disco)) {
            zmsg_t *msg = zmsg_recv (which);
            char *key = zmsg_popstr (msg);
            char *value = zmsg_popstr (msg);
            zsys_debug("Discovered data: key='%s' value='%s'", key, value);
            if(strneq(endpoint, key) && strneq(uuid, value)) {
                zyre_require_peer (node, value, key);
            }
            free (key);
            free (value);
            zmsg_destroy (&msg);
        }
        */
        else
        if (which == control) {
            zmsg_t *msg = zmsg_recv (which);
            //zsys_debug("Got message from control socket");
            //zmsg_print(msg);
            zframe_t *routing_id = zmsg_pop(msg);
            char *command = zmsg_popstr (msg);
            if (streq (command, "SUB")) {
                char *group = zmsg_popstr (msg);
                zsys_debug("Joining %s", group);
                zyre_join (node, group);
                free(group);
            }
            else
            if (streq (command, "PUB")) {
                char *group = zmsg_popstr (msg);
                char *str = zmsg_popstr (msg);
                zsys_debug("pub->zyre %s: %s", group, str);
                zyre_shouts (node, group, "%s", str);
                zstr_sendx (pub, group, "local", str, NULL);
                free(group);
                free(str);
            }
            zframe_destroy(&routing_id);
            zmsg_destroy(&msg);
        }

        if(zclock_mono() - last_bootstrap > 1*1000) {
            connect_to_certs_with_endpoints(node, certstore);
            last_bootstrap = zclock_mono();
        }

    }
    zpoller_destroy (&poller);
    zyre_stop (node);
    zclock_sleep (100);
    zyre_destroy (&node);
}

int keygen()
{
    const char *keypair_filename = "client.key";
    const char *keypair_filename_secret = "client.key_secret";

    if( access( keypair_filename, F_OK ) != -1 ) {
        fprintf(stderr, "%s already exists\n", keypair_filename);
        return 1;
    }
    if( access( keypair_filename_secret, F_OK ) != -1 ) {
        fprintf(stderr, "%s already exists\n", keypair_filename);
        return 1;
    }
    zcert_t *cert = zcert_new();
    if(!cert) {
        perror("Error creating new certificate");
        return 1;
    }
    if(-1 == zcert_save(cert, keypair_filename)) {
        perror("Error writing key to client.key");
        return 1;
    }
    printf("Keys written to %s and %s\n", keypair_filename, keypair_filename_secret);
    return 0;
}

int
main (int argc, char *argv [])
{

    if (argc > 2) {
        puts ("syntax: ./gateway [node_name|keygen]");
        exit (1);
    }
    if (argc == 2 && streq(argv[1], "keygen")) {
        exit(keygen());
    }

    char *endpoint = getenv("ZYRE_BIND");
    if(!endpoint) {
        fprintf(stderr, "Missing ZYRE_BIND env var:\nexport ZYRE_BIND=tcp://*:9200\n");
        exit(1);
    }

    zactor_t *actor = zactor_new (gateway_actor, argv [1]);
    assert (actor);
    
    while (!zsys_interrupted) {
        zclock_sleep(1000);
    }
    zactor_destroy (&actor);
    return 0;
}
