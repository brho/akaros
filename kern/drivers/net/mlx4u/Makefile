obj-$(CONFIG_MLX4_INFINIBAND)	+= mlx4_ib.o

CFLAGS_KERNEL += -include kern/drivers/net/udrvr/compat.h -Wno-pointer-sign

#mlx4_ib-y :=	ah.o cq.o doorbell.o mad.o main.o mr.o qp.o srq.o mcg.o cm.o alias_GUID.o sysfs.o
mlx4_ib-y :=	cq.o doorbell.o main.o mr.o qp.o srq.o
