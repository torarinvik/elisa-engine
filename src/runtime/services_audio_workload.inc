        def audio_workload_snapshot(session: Session&) -> AudioRuntime::WorkloadSnapshot error[RuntimeServicesError, AudioRuntime::AudioError]:
            try require_audio(session)
            try AudioRuntime::workload_snapshot()
