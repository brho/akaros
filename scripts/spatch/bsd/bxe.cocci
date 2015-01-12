// PCI config space.  Looks like the last param is the size of the field

@@
expression DEV;
expression REG;
expression VAL;
@@
-pci_write_config(DEV, REG, VAL, 4);
+pcidev_write32(DEV, REG, VAL);


@@
expression DEV;
expression REG;
expression VAL;
@@
-pci_write_config(DEV, REG, VAL, 2);
+pcidev_write16(DEV, REG, VAL);


@@
expression DEV;
expression REG;
expression VAL;
@@
-pci_write_config(DEV, REG, VAL, 1);
+pcidev_write16(DEV, REG, VAL);


// These didn't work.
@@
expression DEV;
expression REG;
@@
-pci_read_config(DEV, REG, 4);
+pcidev_read32(DEV, REG);

@@
expression DEV;
expression REG;
@@
-pci_read_config(DEV, REG, 2);
+pcidev_read16(DEV, REG);

@@
expression DEV;
expression REG;
@@
-pci_read_config(DEV, REG, 1);
+pcidev_read8(DEV, REG);
