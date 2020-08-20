# Linux driver stack for Arm Ethos-U

The Linux driver stack for Arm Ethos-U provides an example of how a rich
operating system like Linux can dispatch inferences to an Arm Cortex-M
subsystem, consisting of an Arm Cortex-M of choice and an Arm Ethos-U NPU.

## Licenses

The kernel drivers are provided under a GPL v2 license. All other software
componantes are provided under an Apache 2.0 license.

## Building

The driver stack comes with a CMake based build system. Cross compile for an Arm
CPU can for example be done with the provided toolchain file.

```
$ mkdir build
$ cd build
$ cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain/aarch64-linux-gnu.cmake -DKDIR=<Kernel directory>
$ make
```

## DTB

The kernel driver uses the mailbox APIs as a doorbell mechanism.

```
/ {
  reserved-memory {
    #address-cells = <2>;
    #size-cells = <2>;
    ranges;

    ethosu_msg: ethosu_msg@80000000 {
      compatible = "shared-dma-pool";
      reg = <0 0x80000000 0 0x00040000>;
      no-map;
    };

    ethosu_reserved: ethosu_reserved@80040000 {
      compatible = "shared-dma-pool";
      reg = <0 0x80040000 0 0x00040000>;
      no-map;
    };
  };

  ethosu_mailbox: mhu@6ca00000 {
    compatible = "arm,mhu", "arm,primecell";
    reg = <0x0 0x6ca00000 0x0 0x1000>;
    interrupts = <0 168 4>;
    interrupt-names = "npu_rx";
    #mbox-cells = <1>;
    clocks = <&soc_refclk100mhz>;
    clock-names = "apb_pclk";
  };

  ethosu {
    #address-cells = <2>;
    #size-cells = <2>;

    compatible = "arm,ethosu";
    reg = <0 0x80000000 0 0x00010000>,
          <0 0x80010000 0 0x00010000>;
    reg-names = "in_queue", "out_queue";
    memory-region = <&ethosu_reserved>;
    dma-ranges = <0 0x60000000 0 0x80000000 0 0x20000000>;
    mboxes= <&ethosu_mailbox 0>, <&ethosu_mailbox 0>;
    mbox-names = "tx", "rx";
  };
};
```
