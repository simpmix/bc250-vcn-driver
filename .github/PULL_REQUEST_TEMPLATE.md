## Description
Briefly describe the changes introduced in this pull request and the rationale behind them.

## Type of Change
- [ ] Bug fix (non-breaking change fixing an issue)
- [ ] New feature (non-breaking change adding functionality)
- [ ] Performance optimization (shaders, rate control, memory)
- [ ] Documentation update

## Testing Checklist
- [ ] Tested on BC-250 hardware (or compatible Vulkan AMD GPU)
- [ ] Unit tests pass (`ctest --test-dir approach1-compute-encoder/build`)
- [ ] Live encode benchmark tested with `./tools/bc250_diagnose.sh`
- [ ] Audio fix verified across reboot
