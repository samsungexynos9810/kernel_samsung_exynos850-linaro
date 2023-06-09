/*
 * s2mpu10-regulator.h - Voltage regulator driver for the s2mpu10
 *
 *  Copyright (C) 2016 Samsung Electrnoics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __LINUX_MFD_S2MPU10_PRIV_H
#define __LINUX_MFD_S2MPU10_PRIV_H

#include <linux/i2c.h>
#define S2MPU10_REG_INVALID             (0xff)

/* PMIC Top-Level Registers */
#define	S2MPU10_PMIC_REG_VGPIO0		0x00
#define	S2MPU10_PMIC_REG_VGPIO1		0x01
#define	S2MPU10_PMIC_REG_VGPIO2		0x02
#define	S2MPU10_PMIC_REG_VGPIO3		0x03
#define S2MPU10_PMIC_REG_CHIPID		0x04

#define S2MPU10_PMIC_REG_IRQM		0x07
#define S2MPU10_IRQSRC_PMIC			0x01

#define	S2MPU10_PMIC_REG_IBI0		0x14
#define	S2MPU10_PMIC_REG_IBI1		0x15
#define	S2MPU10_PMIC_REG_IBI2		0x16
#define	S2MPU10_PMIC_REG_IBI3		0x17

#define S2MPU10_IBI0_PMIC_S			(1 << 0)
#define S2MPU10_IBI0_CODEC			(1 << 3)
#define S2MPU10_IBI0_PMIC_M			(1 << 4)
#define S2MPU10_IBI1_WRSTBO			(1 << 6)
#define S2MPU10_IBI1_ONOB			(1 << 7)

/* Slave addr = 0xCC */
/* PMIC Registers */
#define S2MPU10_PMIC_REG_INT1		0x00
#define S2MPU10_PMIC_REG_INT2		0x01
#define S2MPU10_PMIC_REG_INT3		0x02
#define S2MPU10_PMIC_REG_INT4		0x03
#define S2MPU10_PMIC_REG_INT5		0x04
#define S2MPU10_PMIC_REG_INT6		0x05
#define S2MPU10_PMIC_REG_INT1M		0x06
#define S2MPU10_PMIC_REG_INT2M		0x07
#define S2MPU10_PMIC_REG_INT3M		0x08
#define S2MPU10_PMIC_REG_INT4M		0x09
#define S2MPU10_PMIC_REG_INT5M		0x0A
#define S2MPU10_PMIC_REG_INT6M		0x0B
#define S2MPU10_PMIC_REG_STATUS1	0x0C
#define S2MPU10_PMIC_REG_STATUS2	0x0D
#define S2MPU10_PMIC_REG_PWRONSRC	0x0E
#define S2MPU10_PMIC_REG_OFFSRC		0x0F

#define S2MPU10_PMIC_REG_BUCHG		0x10
#define S2MPU10_PMIC_REG_RTCBUF		0x11
#define S2MPU10_PMIC_REG_CTRL1		0x12
#define S2MPU10_PMIC_REG_CTRL2		0x13
#define S2MPU10_PMIC_REG_CTRL3		0x14
#define S2MPU10_PMIC_REG_UVLOOTP	0x15
#define S2MPU10_PMIC_REG_CFG1		0x16
#define S2MPU10_PMIC_REG_CFG2		0x17

#define S2MPU10_PMIC_REG_B1CTRL		0x18
#define S2MPU10_PMIC_REG_B1OUT1		0x19
#define S2MPU10_PMIC_REG_B1OUT2		0x1A
#define S2MPU10_PMIC_REG_B2CTRL		0x1B
#define S2MPU10_PMIC_REG_B2OUT1		0x1C
#define S2MPU10_PMIC_REG_B2OUT2		0x1D
#define S2MPU10_PMIC_REG_B3CTRL		0x1E
#define S2MPU10_PMIC_REG_B3OUT1		0x1F
#define S2MPU10_PMIC_REG_B3OUT2		0x20
#define S2MPU10_PMIC_REG_B4CTRL		0x21
#define S2MPU10_PMIC_REG_B4OUT1		0x22
#define S2MPU10_PMIC_REG_B4OUT2		0x23
#define S2MPU10_PMIC_REG_B5CTRL		0x24
#define S2MPU10_PMIC_REG_B5OUT		0x25
#define S2MPU10_PMIC_REG_B6CTRL		0x26
#define S2MPU10_PMIC_REG_B6OUT		0x27
#define S2MPU10_PMIC_REG_B7CTRL		0x28
#define S2MPU10_PMIC_REG_B7OUT1		0x29
#define S2MPU10_PMIC_REG_B7OUT2		0x2A
#define S2MPU10_PMIC_REG_B8CTRL		0x2B
#define S2MPU10_PMIC_REG_B8OUT1		0x2C
#define S2MPU10_PMIC_REG_B8OUT2		0x2D
#define S2MPU10_PMIC_REG_B9CTRL		0x2E
#define S2MPU10_PMIC_REG_B9OUT1		0x2F
#define S2MPU10_PMIC_REG_B9OUT2		0x30

#define S2MPU10_PMIC_REG_BUCK_RISE_RAMP		0x33
#define S2MPU10_PMIC_REG_BUCK_FALL_RAMP		0x34

#define S2MPU10_PMIC_REG_L1CTRL			0x35
#define S2MPU10_PMIC_REG_L2CTRL			0x36
#define S2MPU10_PMIC_REG_L3CTRL			0x37
#define S2MPU10_PMIC_REG_L4CTRL			0x38
#define S2MPU10_PMIC_REG_L5CTRL			0x39
#define S2MPU10_PMIC_REG_L6CTRL			0x3A
#define S2MPU10_PMIC_REG_L7CTRL			0x3B
#define S2MPU10_PMIC_REG_L8CTRL			0x3C
#define S2MPU10_PMIC_REG_L9CTRL			0x3D
#define S2MPU10_PMIC_REG_L10CTRL		0x3E
#define S2MPU10_PMIC_REG_L11CTRL1		0x3F
#define S2MPU10_PMIC_REG_L11CTRL2		0x40
#define S2MPU10_PMIC_REG_L12CTRL		0x41
#define S2MPU10_PMIC_REG_L13CTRL		0x42
#define S2MPU10_PMIC_REG_L14CTRL		0x43
#define S2MPU10_PMIC_REG_L15CTRL		0x44
#define S2MPU10_PMIC_REG_L16CTRL		0x45
#define S2MPU10_PMIC_REG_L17CTRL		0x46
#define S2MPU10_PMIC_REG_L18CTRL		0x47
#define S2MPU10_PMIC_REG_L19CTRL		0x48
#define S2MPU10_PMIC_REG_L20CTRL		0x49
#define S2MPU10_PMIC_REG_L21CTRL		0x4A
#define S2MPU10_PMIC_REG_L22CTRL		0x4B
#define S2MPU10_PMIC_REG_L23CTRL		0x4C
#define S2MPU10_PMIC_REG_L24CTRL		0x4D
#define S2MPU10_PMIC_REG_L25CTRL		0x4E
#define S2MPU10_PMIC_REG_L26CTRL		0x4F

#define S2MPU10_PMIC_SEL_VGPIO0			0x55
#define S2MPU10_PMIC_SEL_VGPIO1			0x56
#define S2MPU10_PMIC_SEL_VGPIO2			0x57
#define S2MPU10_PMIC_SEL_VGPIO3			0x58
#define S2MPU10_PMIC_SEL_VGPIO4			0x59
#define S2MPU10_PMIC_SEL_VGPIO5			0x5A
#define S2MPU10_PMIC_SEL_VGPIO6			0x5B
#define S2MPU10_PMIC_SEL_VGPIO7			0x5C
#define S2MPU10_PMIC_SEL_VGPIO8			0x5D
#define S2MPU10_PMIC_SEL_VGPIO9			0x5E
#define S2MPU10_PMIC_SEL_VGPIO10		0x5F
#define S2MPU10_PMIC_SEL_VGPIO11		0x60
#define S2MPU10_PMIC_SEL_VGPIO12		0x61
#define S2MPU10_PMIC_SEL_VGPIO13		0x62
#define S2MPU10_PMIC_SEL_VGPIO14		0x63
#define S2MPU10_PMIC_SEL_VGPIO15		0x64
#define S2MPU10_PMIC_SEL_VGPIO16		0x65
#define S2MPU10_PMIC_SEL_VGPIO17		0x66

#define S2MPU10_PMIC_OCP_WARN_B2		0x9B
#define S2MPU10_PMIC_OCP_WARN_B3		0x9C
#define S2MPU10_PMIC_OCP_WARN_B4		0x9D
#define S2MPU10_PMIC_BUCK_OI_EN1		0x9E
#define S2MPU10_PMIC_BUCK_OI_EN2		0x9F
#define S2MPU10_PMIC_BUCK_OI_PD_EN1		0xA0
#define S2MPU10_PMIC_BUCK_OI_PD_EN2		0xA1
#define S2MPU10_BUCK_OI_CTRL1			0xA2
#define S2MPU10_BUCK_OI_CTRL2			0xA3
#define S2MPU10_BUCK_OI_CTRL3			0xA4
#define S2MPU10_BUCK_OI_CTRL4			0xA5
#define S2MPU10_BUCK_OI_CTRL5			0xA6

/* S2MPU10 regulator ids */
enum S2MPU10_regulators {
	S2MPU10_LDO1,
	S2MPU10_LDO2,
	S2MPU10_LDO3,
	S2MPU10_LDO4,
	S2MPU10_LDO5,
	S2MPU10_LDO6,
	S2MPU10_LDO7,
	S2MPU10_LDO8,
	S2MPU10_LDO9,
	S2MPU10_LDO10,
/*
	S2MPU10_LDO11,
	S2MPU10_LDO12,
	S2MPU10_LDO13,
	S2MPU10_LDO14,
*/
	S2MPU10_LDO15,
	S2MPU10_LDO16,
	S2MPU10_LDO17,
	S2MPU10_LDO18,
	S2MPU10_LDO19,
	S2MPU10_LDO20,
	S2MPU10_LDO21,
	S2MPU10_LDO22,
	S2MPU10_LDO23,
	S2MPU10_LDO24,
/*
	S2MPU10_LDO25,
	S2MPU10_LDO26,
*/
	S2MPU10_LDO27,
	S2MPU10_BUCK1,
	S2MPU10_BUCK2,
	S2MPU10_BUCK3,
	S2MPU10_BUCK4,
	S2MPU10_BUCK5,
	S2MPU10_BUCK6,
	S2MPU10_BUCK7,
	S2MPU10_BUCK8,
	S2MPU10_BUCK9,
	S2MPU10_REG_MAX,
};

#define S2MPU10_BUCK_MIN1		300000
#define S2MPU10_BUCK_MIN2		600000
#define S2MPU10_LDO_MIN1		725000
#define S2MPU10_LDO_MIN2		300000
#define S2MPU10_LDO_MIN3		700000
#define S2MPU10_LDO_MIN4		1800000
#define S2MPU10_BUCK_STEP1		6250
#define S2MPU10_BUCK_STEP2		12500
#define S2MPU10_LDO_STEP1		12500
#define S2MPU10_LDO_STEP2		25000
#define S2MPU10_LDO_VSEL_MASK	0x3F
#define S2MPU10_BUCK_VSEL_MASK	0xFF
#define S2MPU10_ENABLE_MASK		(0x03 << S2MPU10_ENABLE_SHIFT)
#define S2MPU10_SW_ENABLE_MASK	0x03
#define S2MPU10_RAMP_DELAY		12000

#define S2MPU10_ENABLE_TIME_LDO		128
#define S2MPU10_ENABLE_TIME_BUCK1	110
#define S2MPU10_ENABLE_TIME_BUCK2	110
#define S2MPU10_ENABLE_TIME_BUCK3	110
#define S2MPU10_ENABLE_TIME_BUCK4	150
#define S2MPU10_ENABLE_TIME_BUCK5	150
#define S2MPU10_ENABLE_TIME_BUCK6	150
#define S2MPU10_ENABLE_TIME_BUCK7	150
#define S2MPU10_ENABLE_TIME_BUCK8	150
#define S2MPU10_ENABLE_TIME_BUCK9	150

#define S2MPU10_ENABLE_SHIFT	0x06
#define S2MPU10_LDO_N_VOLTAGES	(S2MPU10_LDO_VSEL_MASK + 1)
#define S2MPU10_BUCK_N_VOLTAGES (S2MPU10_BUCK_VSEL_MASK + 1)

#define S2MPU10_PMIC_EN_SHIFT	6
#define S2MPU10_REGULATOR_MAX (S2MPU10_REG_MAX)
#define SEC_PMIC_REV(iodev)    (iodev)->pmic_rev

#define S2MPU10_OCP_WARN_EN		7
#define S2MPU10_OCP_WARN_CNT		6
#define S2MPU10_OCP_WARN_DVS_MASK	5
/*
 * sec_opmode_data - regulator operation mode data
 * @id: regulator id
 * @mode: regulator operation mode
 */

enum s2mpu10_irq_source {
	PMIC_INT1 = 0,
	PMIC_INT2,
	PMIC_INT3,
	PMIC_INT4,
	PMIC_INT5,
	PMIC_INT6,

	S2MPU10_IRQ_GROUP_NR,
};

#define S2MPU10_NUM_IRQ_PMIC_REGS	6
#define S2MPU10_NUM_SC_LDO_IRQ		4
#define S2MPU10_BUCK_MAX		9
#define S2MPU10_TEMP_MAX		2
#define S2MPU10_OCP_WARN_MASK		0xE0

enum s2mpu10_irq {
	/* PMIC */
	S2MPU10_PMIC_IRQ_PWRONF_INT1,
	S2MPU10_PMIC_IRQ_PWRONR_INT1,
	S2MPU10_PMIC_IRQ_JIGONBF_INT1,
	S2MPU10_PMIC_IRQ_JIGONBR_INT1,
	S2MPU10_PMIC_IRQ_ACOKF_INT1,
	S2MPU10_PMIC_IRQ_ACOKR_INT1,
	S2MPU10_PMIC_IRQ_PWRON1S_INT1,
	S2MPU10_PMIC_IRQ_MRB_INT1,

	S2MPU10_PMIC_IRQ_RTC60S_INT2,
	S2MPU10_PMIC_IRQ_RTCA1_INT2,
	S2MPU10_PMIC_IRQ_RTCA0_INT2,
	S2MPU10_PMIC_IRQ_SMPL_INT2,
	S2MPU10_PMIC_IRQ_RTC1S_INT2,
	S2MPU10_PMIC_IRQ_WTSR_INT2,
	S2MPU10_PMIC_IRQ_ADCDONE_INT2,
	S2MPU10_PMIC_IRQ_WTSRB_INT2,

	S2MPU10_PMIC_IRQ_OCPB1_INT3,
	S2MPU10_PMIC_IRQ_OCPB2_INT3,
	S2MPU10_PMIC_IRQ_OCPB3_INT3,
	S2MPU10_PMIC_IRQ_OCPB4_INT3,
	S2MPU10_PMIC_IRQ_OCPB5_INT3,
	S2MPU10_PMIC_IRQ_OCPB6_INT3,
	S2MPU10_PMIC_IRQ_OCPB7_INT3,
	S2MPU10_PMIC_IRQ_OCPB8_INT3,

	S2MPU10_PMIC_IRQ_OCPB9_INT4,
	S2MPU10_PMIC_IRQ_OCPB10_INT4,
	S2MPU10_PMIC_IRQ_UV_BB_INT4,
	S2MPU10_PMIC_IRQ_BB_NTR_DET_INT4,
	S2MPU10_PMIC_IRQ_SC_LDO1_INT4,
	S2MPU10_PMIC_IRQ_SC_LDO2_INT4,
	S2MPU10_PMIC_IRQ_SC_LDO7_INT4,
	S2MPU10_PMIC_IRQ_SC_LDO19_INT4,

	S2MPU10_PMIC_IRQ_OI_B1_INT5,
	S2MPU10_PMIC_IRQ_OI_B2_INT5,
	S2MPU10_PMIC_IRQ_OI_B3_INT5,
	S2MPU10_PMIC_IRQ_OI_B4_INT5,
	S2MPU10_PMIC_IRQ_OI_B5_INT5,
	S2MPU10_PMIC_IRQ_OI_B6_INT5,
	S2MPU10_PMIC_IRQ_OI_B7_INT5,
	S2MPU10_PMIC_IRQ_OI_B8_INT5,

	S2MPU10_PMIC_IRQ_OI_B9_INT6,
	S2MPU10_PMIC_IRQ_OI_BB_INT6,
	S2MPU10_PMIC_IRQ_INT120C_INT6,
	S2MPU10_PMIC_IRQ_INT140C_INT6,
	S2MPU10_PMIC_IRQ_TSD_INT6,
	S2MPU10_PMIC_IRQ_TIMEOUT2_INT6,
	S2MPU10_PMIC_IRQ_TIMEOUT3_INT6,
	S2MPU10_PMIC_IRQ_SUB_SMPL_INT6,
	S2MPU10_IRQ_NR,
};


enum sec_device_type {
	S2MPU10X,
};

struct s2mpu10_dev {
	struct device *dev;
	struct i2c_client *i2c;
	struct i2c_client *pmic;
	struct i2c_client *rtc;
	struct mutex i2c_lock;
	struct apm_ops *ops;

	int type;
	int device_type;
	int irq;
	int irq_base;
	bool wakeup;
	struct mutex irqlock;
	int irq_masks_cur[S2MPU10_IRQ_GROUP_NR];
	int irq_masks_cache[S2MPU10_IRQ_GROUP_NR];

	/* pmic REV register */
	u8 pmic_rev;	/* pmic Rev */

	struct s2mpu10_platform_data *pdata;
};

enum s2mpu10_types {
	TYPE_S2MPU10,
};

extern int s2mpu10_irq_init(struct s2mpu10_dev *s2mpu10);
extern void s2mpu10_irq_exit(struct s2mpu10_dev *s2mpu10);

/* S2MPU10 shared i2c API function */
extern int s2mpu10_read_reg(struct i2c_client *i2c, u8 reg, u8 *dest);
extern int s2mpu10_bulk_read(struct i2c_client *i2c, u8 reg, int count,
				u8 *buf);
extern int s2mpu10_write_reg(struct i2c_client *i2c, u8 reg, u8 value);
extern int s2mpu10_bulk_write(struct i2c_client *i2c, u8 reg, int count,
				u8 *buf);
extern int s2mpu10_write_word(struct i2c_client *i2c, u8 reg, u16 value);
extern int s2mpu10_read_word(struct i2c_client *i2c, u8 reg);

extern int s2mpu10_update_reg(struct i2c_client *i2c, u8 reg, u8 val, u8 mask);

extern int s2mpu10_read_pwron_status(void);

#endif /* __LINUX_MFD_S2MPU10_PRIV_H */
