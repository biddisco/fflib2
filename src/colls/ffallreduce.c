#include "ffcollectives.h"
#include <assert.h>

#define TMPMEM(MEM, TYPE, BSIZE, OFF) (void *) &(((uint8_t *) MEM)[((BSIZE)*(OFF))])

//#define FFALLREDYCE_REDUCE_BCAST

#ifdef FFALLREDUCE_REDUCE_BCAST
int ffallreduce(void * sndbuff, void * rcvbuff, int count, int tag, ffoperator_h operator, ffdatatype_h datatype, ffschedule_h * _sched){
 
    ffschedule_h sched;
    FFCALL(ffschedule_create(&sched));

    int p, rank;
    ffsize(&p);
    ffrank(&rank);
    int maxr = (int)ceil((log2(p)));
    int vpeer, vrank, peer, root=0;

    size_t unitsize;
    ffdatatype_size(datatype, &unitsize);

    RANK2VRANK(rank, vrank, root);

    int nchild = ceil(log(vrank==0 ? p : vrank)) + 1;
    
    void * tmpmem = malloc(nchild*count*unitsize);
    ffschedule_set_tmpmem(sched, tmpmem);

    //TODO: MAKE COLLECTIVE TAG

    // reduce part 
    ffop_h send_up;
    ffnop(0, &send_up);



    ffop_h move;
    ffcomp(sndbuff, NULL, count, datatype, FFIDENTITY, 0, rcvbuff, &move);

    ffop_h comp=FFNONE, recv=FFNONE;

    for(int r=1; r<=maxr; r++) {
        if((vrank % (1<<r)) == 0) {
            /* we have to receive this round */
            vpeer = vrank + (1<<(r-1));
            VRANK2RANK(peer, vpeer, root)

            if(peer<p) {

                FFLOG("nchild: %i; tmpmem: %p; r: %i; peer: %i; count: %i; unitsize: %lu\n", nchild, TMPMEM(tmpmem, datatype, count*unitsize, r-1), r, peer, count, unitsize);

                assert(nchild > r-1);

                //Receive from the peer
                ffrecv(TMPMEM(tmpmem, datatype, count*unitsize, r-1), count, datatype, peer, tag, 0, &recv); 
             
                //accumulate
                ffcomp(TMPMEM(tmpmem, datatype, count*unitsize, r-1), rcvbuff, count, datatype, operator, FFCOMP_DEST_ATOMIC, rcvbuff, &comp); 

                //we need to receive before start computing
                ffop_hb(recv, comp);

                //wait for the rcvbuff (our accumulator) to be ready before sending
                ffop_hb(move, comp);

                //we need to compute everything before sending
                ffop_hb(comp, send_up);
           
                ffschedule_add_op(sched, recv); 
                ffschedule_add_op(sched, comp);   
            }
        }else{
            ffop_h send;
       
            /* we have to send this round */
            vpeer = vrank - (1<<(r-1));
            VRANK2RANK(peer, vpeer, root)

            //send
            ffsend(rcvbuff, count, datatype, peer, tag, 0, &send);
    
            //receive & reduce data from children before sending it up
            ffop_hb(send_up, send);
            
            ffschedule_add_op(sched, send);

            break;
        }
    }
 
    if (recv==FFNONE) ffop_hb(move, send_up); 
    ffschedule_add_op(sched, move);

    // broadcast
    RANK2VRANK(rank, vrank, root);

    recv = FFNONE;
    ffop_h recv_before_send;
    ffnop(0, &recv_before_send);

    /* receive from the right hosts  */
    if(vrank != 0) {
        for(int r=0; r<maxr; r++) {
            if((vrank >= (1<<r)) && (vrank < (1<<(r+1)))) {
                VRANK2RANK(peer, vrank-(1<<r), root);

                //recv
                ffrecv(rcvbuff, count, datatype, peer, tag, 0, &recv);               
    
                ffop_hb(recv, recv_before_send);

                ffschedule_add_op(sched, recv);
            }
        }
    }

    // at the root we need to wait to receive before sending down
    if (recv == FFNONE) {
        ffop_hb(send_up, recv_before_send);
    }

    // now send to the right hosts 
    for(int r=0; r<maxr; r++) {
        if(((vrank + (1<<r) < p) && (vrank < (1<<r))) || (vrank == 0)) {
            VRANK2RANK(peer, vrank+(1<<r), root);

            ffop_h send;
    
            //send
            ffsend(rcvbuff, count, datatype, peer, tag, 0, &send);

            ffop_hb(recv_before_send, send);

            ffschedule_add_op(sched, send);
        }
    }

    ffschedule_add_op(sched, send_up);
    ffschedule_add_op(sched, recv_before_send);

    *_sched = sched;

    return FFSUCCESS;
}


#else
//Recursive doubling
int ffallreduce(void * sndbuff, void * rcvbuff, int count, int tag, ffoperator_h operator, ffdatatype_h datatype, ffschedule_h * _sched){

    ffschedule_h sched;
    FFCALL(ffschedule_create(&sched));

    int csize, rank;
    ffsize(&csize);
    ffrank(&rank);

    int mask = 0x1;
    int maxr = (int)ceil((log2(csize)));

    size_t unitsize;
    ffdatatype_size(datatype, &unitsize);

    FFLOG("allocating mem %lu (maxr = %u)\n", (maxr)*count*unitsize, maxr+1);
    void * tmpmem = malloc(maxr*count*unitsize);
    ffschedule_set_tmpmem(sched, tmpmem);

    ffop_h move;
    ffcomp(sndbuff, NULL, count, datatype, FFIDENTITY, FFCOMP_DEST_ATOMIC, rcvbuff, &move);

    ffop_h send=FFNONE, recv=FFNONE, prev_send=FFNONE, comp=FFNONE;
    uint32_t r=0;

    while (mask < csize) {
        uint32_t dst = rank^mask;
        if (dst < csize) {

            ffsend(rcvbuff, count, datatype, dst, tag, 0, &send);

            if (comp==FFNONE){
                //first send --> only wait for the move
                ffop_hb(move, send);
            }else{
                //before sending we have to wait for the computation
                ffop_hb(comp, send);
            }

            //a send has to wait on all prev computations as well.. here 
            //we just serialize the sends to avoid too many deps.
            if (prev_send != FFNONE){
                ffop_hb(prev_send, send);
            }
            prev_send = send;            

            ffschedule_add_op(sched, send);

            //receive from the peer
            ffrecv(TMPMEM(tmpmem, datatype, count*unitsize, r), count, 
                    datatype, dst, tag, 0, &recv); 
             
            //accumulate
            ffcomp(TMPMEM(tmpmem, datatype, count*unitsize, r), rcvbuff, count, 
                    datatype, operator, FFCOMP_DEST_ATOMIC, rcvbuff, &comp); 

            //comp has to wait the receive to happen
            ffop_hb(recv, comp);
    
            ffschedule_add_op(sched, recv);
            ffschedule_add_op(sched, comp);   
            
            r++; 
        }
        mask <<= 1;
    }

    ffschedule_add_op(sched, move);

    *_sched = sched;
    return FFSUCCESS;
}

#endif
