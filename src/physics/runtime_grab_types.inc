        enum GrabKind:
            Fixed
            Point

        struct GrabHandle:
            private:
                world_generation: u64
                slot: u32
                generation: u64
