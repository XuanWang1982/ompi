/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2012 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2008 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2007      Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2012-2013 Los Alamos National Security, LLC.  All rights
 *                         reserved.
 * Copyright (c) 2014      Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"
#include <stdio.h>

#include "ompi/mpi/c/bindings.h"
#include "ompi/runtime/params.h"
#include "ompi/communicator/communicator.h"
#include "ompi/errhandler/errhandler.h"
#include "ompi/datatype/ompi_datatype.h"
#include "ompi/memchecker.h"

#if OPAL_HAVE_WEAK_SYMBOLS && OMPI_PROFILING_DEFINES
#pragma weak MPI_Neighbor_alltoallv = PMPI_Neighbor_alltoallv
#endif

#if OMPI_PROFILING_DEFINES
#include "ompi/mpi/c/profile/defines.h"
#endif

static const char FUNC_NAME[] = "MPI_Neighbor_alltoallv";


int MPI_Neighbor_alltoallv(const void *sendbuf, const int sendcounts[], const int sdispls[],
                           MPI_Datatype sendtype, void *recvbuf,
                           const int recvcounts[], const int rdispls[],
                           MPI_Datatype recvtype, MPI_Comm comm)
{
    int i, size, err;
    size_t sendtype_size, recvtype_size;
    bool zerosend=true, zerorecv=true;

    MEMCHECKER(
        ptrdiff_t recv_ext;
        ptrdiff_t send_ext;

        memchecker_datatype(sendtype);
        memchecker_datatype(recvtype);

        ompi_datatype_type_extent(recvtype, &recv_ext);
        ompi_datatype_type_extent(sendtype, &send_ext);

        memchecker_comm(comm);

        size = OMPI_COMM_IS_INTER(comm)?ompi_comm_remote_size(comm):ompi_comm_size(comm);
        for ( i = 0; i < size; i++ ) {
            /* check if send chunks are defined. */
            memchecker_call(&opal_memchecker_base_isdefined,
                            (char *)(sendbuf)+sdispls[i]*send_ext,
                            sendcounts[i], sendtype);
            /* check if receive chunks are addressable. */
            memchecker_call(&opal_memchecker_base_isaddressable,
                            (char *)(recvbuf)+rdispls[i]*recv_ext,
                            recvcounts[i], recvtype);
        }
    );

    if (MPI_PARAM_CHECK) {

        /* Unrooted operation -- same checks for all ranks */

        err = MPI_SUCCESS;
        OMPI_ERR_INIT_FINALIZE(FUNC_NAME);
        if (ompi_comm_invalid(comm) || !(OMPI_COMM_IS_CART(comm) || OMPI_COMM_IS_GRAPH(comm) ||
                                         OMPI_COMM_IS_DIST_GRAPH(comm))) {
            return OMPI_ERRHANDLER_INVOKE(MPI_COMM_WORLD, MPI_ERR_COMM,
                                          FUNC_NAME);
        }

        if (MPI_IN_PLACE == sendbuf) {
            sendcounts = recvcounts;
            sdispls = rdispls;
            sendtype = recvtype;
        }

        if ((NULL == sendcounts) || (NULL == sdispls) ||
            (NULL == recvcounts) || (NULL == rdispls) ||
            MPI_IN_PLACE == recvbuf) {
            return OMPI_ERRHANDLER_INVOKE(comm, MPI_ERR_ARG, FUNC_NAME);
        }

        size = OMPI_COMM_IS_INTER(comm)?ompi_comm_remote_size(comm):ompi_comm_size(comm);
        for (i = 0; i < size; ++i) {
            OMPI_CHECK_DATATYPE_FOR_SEND(err, sendtype, sendcounts[i]);
            OMPI_ERRHANDLER_CHECK(err, comm, err, FUNC_NAME);
            OMPI_CHECK_DATATYPE_FOR_RECV(err, recvtype, recvcounts[i]);
            OMPI_ERRHANDLER_CHECK(err, comm, err, FUNC_NAME);
        }

        if (MPI_IN_PLACE != sendbuf && !OMPI_COMM_IS_INTER(comm)) {
            int me = ompi_comm_rank(comm);
            ompi_datatype_type_size(sendtype, &sendtype_size);
            ompi_datatype_type_size(recvtype, &recvtype_size);
            if ((sendtype_size*sendcounts[me]) != (recvtype_size*recvcounts[me])) {
                return OMPI_ERRHANDLER_INVOKE(comm, MPI_ERR_TRUNCATE, FUNC_NAME);
            }
        }
    }

    /* Do we need to do anything? */

    ompi_datatype_type_size(sendtype, &sendtype_size);
    ompi_datatype_type_size(recvtype, &recvtype_size);
    size = OMPI_COMM_IS_INTER(comm)?ompi_comm_remote_size(comm):ompi_comm_size(comm);
    if (0 != recvtype_size) {
        for (i = 0; i < size; ++i) {
            if (0 != recvcounts[i]) {
                zerorecv = false;
                break;
            }
        }
    }
    if (MPI_IN_PLACE == sendbuf) {
        zerosend = zerorecv;
    } else if (0 != sendtype_size) {
        for (i = 0; i < size; ++i) {
            if (0 != sendcounts[i]) {
                zerosend = false;
                break;
            }
        }
    }
    if (zerosend && zerorecv) {
        return MPI_SUCCESS;
    }

    OPAL_CR_ENTER_LIBRARY();

    /* Invoke the coll component to perform the back-end operation */
    /* XXX -- CONST -- do not cast away const -- update mca/coll */
    err = comm->c_coll.coll_neighbor_alltoallv((void *) sendbuf, (int *) sendcounts, (int *) sdispls, sendtype,
                                               recvbuf, (int *) recvcounts, (int *) rdispls, recvtype,
                                               comm, comm->c_coll.coll_neighbor_alltoallv_module);
    OMPI_ERRHANDLER_RETURN(err, comm, err, FUNC_NAME);
}

