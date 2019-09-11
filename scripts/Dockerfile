FROM alpine AS build
MAINTAINER chriseth <chris@ethereum.org>
#Official solidity docker image

# Values: `ON` or `OFF`
ARG BUILD_LLL="OFF"

#Establish working directory as solidity
WORKDIR /solidity

# Build dependencies
ADD /scripts/install_deps.sh /solidity/scripts/install_deps.sh
RUN ./scripts/install_deps.sh

#Copy working directory on travis to the image
COPY / $WORKDIR

# Number of parallel jobs during build
# or 0 for auto-computing (max(1, CPU_core_count * 2/3), a greedy value)
ARG BUILD_CONCURRENCY="0"

#Install dependencies, eliminate annoying warnings
RUN sed -i -E -e 's/include <sys\/poll.h>/include <poll.h>/' /usr/include/boost/asio/detail/socket_types.hpp
RUN cmake -DCMAKE_BUILD_TYPE=Release -DTESTS=0 -DLLL=${BUILD_LLL} -DSOLC_LINK_STATIC=1 -DLLLC_LINK_STATIC=1
RUN make solc $([[ ! x"${BUILD_LLL}" = x'OFF' ]] && echo 'lllc')\
    -j$(awk "BEGIN {                                       \
        if (${BUILD_CONCURRENCY} != 0) {                   \
            print(${BUILD_CONCURRENCY});                   \
        } else {                                           \
            x=($(grep -c ^processor /proc/cpuinfo) * 2/3); \
            if (x > 1) {                                   \
                printf(\"%d\n\", x);                       \
            } else {                                       \
                print(1);                                  \
            }                                              \
        }                                                  \
    }")
RUN strip solc/solc && ( [[ x"${BUILD_LLL}" = x'OFF' ]] || strip lllc/lllc )

FROM scratch AS lllc
COPY --from=build /solidity/lllc/lllc /usr/bin/lllc
ENTRYPOINT ["/usr/bin/lllc"]

FROM scratch AS solc
COPY --from=build /solidity/solc/solc /usr/bin/solc
ENTRYPOINT ["/usr/bin/solc"]
