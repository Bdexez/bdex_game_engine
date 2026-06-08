# Engine libraries, listed from the lowest layer to the highest.
# Each one only depends on the layers above it in this list.
add_subdirectory(core)      # kernel: modules, services, events, scheduler, jobs
