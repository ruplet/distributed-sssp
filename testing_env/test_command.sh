# #!/bin/bash

# if [ -z ${OMPI_COMM_WORLD_LOCAL_RANK+x} ]; then RANK=$SLURM_PROCID; else RANK=$OMPI_COMM_WORLD_LOCAL_RANK; fi

if [ -n "${OMPI_COMM_WORLD_RANK}" ]; then # Open MPI global rank
    RANK=${OMPI_COMM_WORLD_RANK}
elif [ -n "${PMI_RANK}" ]; then # MPICH/MVAPICH
    RANK=${PMI_RANK}
elif [ -n "${I_MPI_RANK}" ]; then # Intel MPI
    RANK=${I_MPI_RANK}
elif [ -n "${SLURM_PROCID}" ]; then # SLURM (often the fallback)
    RANK=${SLURM_PROCID}
else
    echo "Warning: Could not determine MPI rank from environment variables."
    RANK="unknown" # Or exit, or default to 0 if for single-process fallback
fi

./$1/sssp tests/$2/$RANK.in outputs/$RANK.out