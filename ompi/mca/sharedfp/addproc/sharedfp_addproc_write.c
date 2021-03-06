/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2013      University of Houston. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */


#include "ompi_config.h"
#include "sharedfp_addproc.h"

#include "mpi.h"
#include "ompi/constants.h"
#include "ompi/mca/sharedfp/sharedfp.h"

int mca_sharedfp_addproc_write (mca_io_ompio_file_t *fh,
                                void *buf,
                                int count,
                                struct ompi_datatype_t *datatype,
                                ompi_status_public_t *status)
{
    int ret = OMPI_SUCCESS;
    OMPI_MPI_OFFSET_TYPE offset = 0;
    long bytesRequested = 0;
    size_t numofBytes;
    struct mca_sharedfp_base_data_t *sh = NULL;
    mca_sharedfp_base_module_t * shared_fp_base_module = NULL;

    if(NULL == fh->f_sharedfp_data){
	if ( mca_sharedfp_addproc_verbose ) {
	    printf("sharedfp_addproc_write:  opening the shared file pointer file\n");
	}
        shared_fp_base_module = fh->f_sharedfp;

        ret = shared_fp_base_module->sharedfp_file_open(fh->f_comm,
                                                        fh->f_filename,
                                                        fh->f_amode,
                                                        fh->f_info,
                                                        fh);
        if ( OMPI_SUCCESS != ret ) {
            opal_output(0,"sharedfp_addproc_write - error opening the shared file pointer\n");
            return ret;
        }
    }

    /* Calculate the number of bytes to write*/
    opal_datatype_type_size ( &datatype->super, &numofBytes);
    bytesRequested = count * numofBytes;

    /*Retrieve the shared file data structure */
    sh = fh->f_sharedfp_data;

    if ( mca_sharedfp_addproc_verbose ){
	printf("sharedfp_addproc_write: sharedfp_addproc_write: Bytes Requested is %ld\n",bytesRequested);
    }

    /*Request the offset to write bytesRequested bytes*/
    ret = mca_sharedfp_addproc_request_position( sh, bytesRequested, &offset);
    if ( OMPI_SUCCESS == ret ) {
	if ( mca_sharedfp_addproc_verbose ){
	    printf("sharedfp_addproc_write: Offset received is %lld\n",offset);
	}
        /* Write to the file */
        ret = ompio_io_ompio_file_write_at(sh->sharedfh,offset,buf,count,datatype,status);
    }

    return ret;
}

int mca_sharedfp_addproc_write_ordered (mca_io_ompio_file_t *fh,
                                        void *buf,
                                        int count,
                                        struct ompi_datatype_t *datatype,
                                        ompi_status_public_t *status)
{
    int ret = OMPI_SUCCESS;
    OMPI_MPI_OFFSET_TYPE offset = 0, offsetReceived = 0;
    long sendBuff = 0;
    long *buff=NULL;
    long offsetBuff;
    long bytesRequested = 0;
    int recvcnt = 1, sendcnt = 1;
    size_t numofBytes;
    int rank, size, i;
    struct mca_sharedfp_base_data_t *sh = NULL;
    mca_sharedfp_base_module_t * shared_fp_base_module = NULL;

    if ( NULL == fh->f_sharedfp_data){
	if ( mca_sharedfp_addproc_verbose ) {
	    printf("sharedfp_addproc_write_ordered:  opening the shared file pointer\n");
	}
        shared_fp_base_module = fh->f_sharedfp;

        ret = shared_fp_base_module->sharedfp_file_open(fh->f_comm,
                                                        fh->f_filename,
                                                        fh->f_amode,
                                                        fh->f_info,
                                                        fh);
        if ( OMPI_SUCCESS != ret ) {
            opal_output(0,"sharedfp_addproc_write_ordered - error opening the shared file pointer\n");
            return ret;
        }
    }

    /*Retrieve the shared file pointer structure*/
    sh = fh->f_sharedfp_data;

    /* Calculate the number of bytes to write*/
    opal_datatype_type_size ( &datatype->super, &numofBytes);
    sendBuff = count * numofBytes;

    /* Get the ranks in the communicator */
    rank = ompi_comm_rank ( sh->comm );
    size = ompi_comm_size ( sh->comm );

    if ( 0  == rank ) {
        buff = (long*)malloc(sizeof(long) * size);
        if ( NULL ==  buff )
            return OMPI_ERR_OUT_OF_RESOURCE;
    }

    ret = sh->comm->c_coll.coll_gather ( &sendBuff, sendcnt, OMPI_OFFSET_DATATYPE, buff,
					 recvcnt, OMPI_OFFSET_DATATYPE, 0, sh->comm,
					 sh->comm->c_coll.coll_gather_module);
    if( OMPI_SUCCESS != ret ){
	goto exit;
    }

    /* All the counts are present now in the recvBuff.
       The size of recvBuff is sizeof_newComm
     */
    if ( 0 == rank ) {
        for (i = 0; i < size ; i ++) {
            bytesRequested += buff[i];

	    if ( mca_sharedfp_addproc_verbose ){
		printf("sharedfp_addproc_write_ordered: Bytes requested are %ld\n",bytesRequested);
	    }
	}

        /* Request the offset to write bytesRequested bytes
	** only the root process needs to do the request,
	** since the root process will then tell the other
	** processes at what offset they should write their
	** share of the data.
	*/
        ret = mca_sharedfp_addproc_request_position(sh,bytesRequested,&offsetReceived);
        if( OMPI_SUCCESS != ret ){
            goto exit;
        }
	if ( mca_sharedfp_addproc_verbose ){
	    printf("sharedfp_addproc_write_ordered: Offset received is %lld\n",offsetReceived);
	}
        buff[0] += offsetReceived;

        for (i = 1 ; i < size; i++)  {
            buff[i] += buff[i-1];
        }
    }

    /* Scatter the results to the other processes*/
    ret = sh->comm->c_coll.coll_scatter ( buff, sendcnt, OMPI_OFFSET_DATATYPE, &offsetBuff,
					  recvcnt, OMPI_OFFSET_DATATYPE, 0, sh->comm,
					  sh->comm->c_coll.coll_scatter_module );
    if( OMPI_SUCCESS != ret ){
	goto exit;
    }

    /*Each process now has its own individual offset in recvBUFF*/
    offset = offsetBuff - sendBuff;

    if ( mca_sharedfp_addproc_verbose ){
	printf("sharedfp_addproc_write_ordered: Offset returned is %lld\n",offset);
    }

    /* write to the file */
    ret = ompio_io_ompio_file_write_at_all(sh->sharedfh,offset,buf,count,datatype,status);

exit:
    if ( NULL != buff ) {
	free ( buff );
    }
    return ret;
}
