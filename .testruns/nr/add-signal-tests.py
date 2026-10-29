exec(Path('.testruns/nr/add-signal-diagnostic.py').read_text().split("edit('native/neural/src/diagnostics.hpp'")[0])

edit('tools/harness/neural_stages.py', [
    ("row = metadata[number]\n", "row = metadata[number]\n        input_scale = float(row.get('inputScale', 1))\n        if not np.isfinite(input_scale) or abs(input_scale - requested.get('neuralInputScale', 1)) > 1e-6:\n            raise ValueError('NR input conditioning differs from the plan')\n"),
    ("result = dict(frame_ids=", "result = dict(input_scale=float(requested.get('neuralInputScale', 1)), frame_ids=")])

edit('tests/neural_stages_test.py', [
    ('    def test_replay_reports_output_drift_with_constant_inputs(self):', '''    def test_input_conditioning_must_match_the_plan(self):
        self.request['neuralInputScale'] = 4
        with self.assertRaises(ValueError): self.result()
        for row in self.rows: row['inputScale'] = 4
        self.assertEqual(self.result()['input_scale'], 4)
        self.rows[-1]['inputScale'] = .25
        with self.assertRaises(ValueError): self.result()

    def test_replay_reports_output_drift_with_constant_inputs(self):'''),
    ("    mutations = [", "    mutations = [\n        (\"abs(input_scale - requested.get('neuralInputScale', 1)) > 1e-6\", 'False', 'test_input_conditioning_must_match_the_plan'),")])

edit('tests/neural_gpu_smoke.cpp', [
    ('        std::puts("PASS: real NGX evaluation,', '''        if(diagnostics) {
            auto conditioning=symbol<int(__cdecl*)(float)>(nr,"TGNR_SetDiagnosticInputScale");
            if(conditioning(4)==0) throw std::runtime_error("Input conditioning escaped diagnostic scope");
            constexpr UINT size=256;
            std::vector<float> colors(size*size*4),depths(size*size,.5f),motion(size*size*2,0);
            for(UINT y=0;y<size;++y) for(UINT x=0;x<size;++x) {
                auto i=4*(y*size+x); colors[i]=float(x)/32; colors[i+1]=float(y)/64; colors[i+2]=.125f; colors[i+3]=.37f;
            }
            auto color=texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),size*16);
            auto depth=texture(size,size,DXGI_FORMAT_R32_FLOAT,depths.data(),size*4);
            auto mv=texture(size,size,DXGI_FORMAT_R32G32_FLOAT,motion.data(),size*8);
            uint32_t number=4000;
            for(float scale:{.25f,1.f,4.f,1.f}) {
                auto folder=fs::absolute(argv[3])/("signal-"+std::to_string(++number));
                if(diagnosticOutput(folder.c_str())) throw std::runtime_error("Input diagnostic output rejected");
                // The final case deliberately omits the setter to verify automatic restoration.
                if(number!=4004 && conditioning(scale)) throw std::runtime_error("Input conditioning rejected");
                for(float bad:{0.f,-1.f,INFINITY,NAN})
                    if(!conditioning(bad)) throw std::runtime_error("Invalid input conditioning accepted");
                // Nonzero strength exercises the HDR output shader, unlike the exact-zero bypass.
                TGNR_Frame f{sizeof(f),TGNR_ABI,number,1,reinterpret_cast<uint64_t>(color.Get()),reinterpret_cast<uint64_t>(depth.Get()),reinterpret_cast<uint64_t>(mv.Get()),size,size,size,size,11,1e-8f,0,0,.1f,1000};
                f.localTone=f.localStructure=f.skinStructure=1;
                auto guides=submit(&f,1); if(!guides) throw std::runtime_error("Signal guides rejected"); render(1,guides);
                auto scene=submit(&f,2); if(!scene) throw std::runtime_error("Signal scene rejected"); diagnosticExposure(scene,exposure.Get()); render(2,scene);
                read(&status); if(status.state!=TGNR_READY) throw std::runtime_error("Signal evaluation failed");
                const auto output=pixels(color.Get(),size); double worst=0;
                for(size_t i=0;i<output.size();++i) worst=std::max(worst,double(std::abs(output[i]-colors[i])/std::max(.001f,std::abs(colors[i]))));
                std::printf("Signal scale=%.2f inverse error=%.9f\\n",scale,worst);
                if(!std::isfinite(worst) || worst>.001) throw std::runtime_error("Input conditioning inverse changed near-neutral HDR");
                context->UpdateSubresource(color.Get(),0,nullptr,colors.data(),size*16,0);
                diagnosticOutput(nullptr);
                if(conditioning(4)==0) throw std::runtime_error("Input conditioning survived diagnostic shutdown");
            }
            render(3,nullptr);
        }
        std::puts("PASS: real NGX evaluation,''')])

edit('tests/neural_stage_probe.py', [
    ("    print('PASS: stage orientation,", '''    for frame, scale in [(4001, .25), (4002, 1), (4003, 4), (4004, 1)]:
        _, stages, _ = read_frame(folder / f'signal-{frame}' / f'nr-{frame}.bin')
        x, y = np.meshgrid(np.arange(256) / 32, np.arange(256) / 64)
        original = np.stack([x, y, np.full_like(x, .125)], axis=-1)[::-1]
        linear = original * scale
        linear /= 1 + linear.max(axis=-1, keepdims=True)
        encoded = np.where(linear <= .0031308, linear * 12.92, 1.055 * linear ** (1 / 2.4) - .055)
        for iy in range(36):
            for ix in range(64):
                ys = slice(iy * 256 // 36, (iy+1) * 256 // 36)
                xs = slice(ix * 256 // 64, (ix+1) * 256 // 64)
                np.testing.assert_allclose(stages[1, iy, ix, :3], encoded[ys, xs].mean((0, 1)), atol=2e-6,
                                           err_msg=f'Wrong conditioning: {scale}')
        np.testing.assert_allclose(stages[0, ..., :3], stages[3, ..., :3], rtol=.001, atol=2e-6)
    print('PASS: conditioning and restoration, stage orientation,''')])
