/*
 * Copyright (C) 2015  University of Oregon
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Apache License, as specified in the LICENSE file.
 *
 * For more information, see the LICENSE file.
 */
#define icat_top_target_device xc3s700an
#define icat_top_target_package fgg484
#define icat_top_target_speed_grade -4
#define ICAT_CLK_PERIOD 12.5
#define ICAT_CLK_PHASE_SHIFT 0
#define ICAT_CLK_PHASE_SHIFT_WIDTH 9
#define ICAT_CHIPSCOPE 0
#define ICAT_DATA_PORT_WIDTH 64
#define ICAT_SAMPLE_DATA_DEPTH 1024
#define ICAT_MAX_DELAY 64
#define ICAT_MAX_XMIT_LO_CUT_THROUGH_DELAY 4
#define ICAT_XMIT_LO_POS 1
#define ICAT_AMP_LIN_TABLE_DEPTH 512
#define ICAT_AMP_GAIN_COMP_TABLE_DEPTH 64
#define ICAT_ATTEN_OUTPUT_WIDTH 24
#define ICAT_DELAY_OUTPUT_WIDTH 7
#define ICAT_ATTEN_TABLE_DEPTH 512
#define ICAT_LOG_ATTEN_TABLE_DEPTH 9
#define ICAT_ATTEN_TABLE_BUS_DEPTH 1024
#define ICAT_AUX_OUTPUT_WIDTH 6
#define ICAT_AUX_ADDRESS_WIDTH 3
#define ICAT_AUX_WORD_WIDTH 8
#define ICAT_AUX_TEMP_UPDATE_ADDR 5
#define ICAT_AUX_TEMP_UPDATE_DISABLE 0
#define ICAT_AUX_TEMP_UPDATE_ENABLE 1
#define ICAT_AUX_TEMP_UPDATE_CONTINUOUS 2
#define ICAT_AUX_BUS_ADDR 2
#define ICAT_AUX_ATTEN_ADDR 6
#define ICAT_AUX_ALT_RELAY_ADDR 7
#define ICAT_NOISE_FILTER_COEFF_BUS_WIDTH (int((18+16-1)/16))
#define ICAT_NOISE_FILTER_COEFF_WIDTH 18
#define ICAT_NOISE_FILTER_ORDER 62
#define ICAT_NOISE_FILTER_SYMMETRIC 1
#define ICAT_NOISE_FILTER_NUM_COEFF (int(62/(1<<1))+1)
#define ICAT_LED_WIDTH 8
#define ICAT_DAC_WIDTH 16
#define ICAT_PHASE_WIDTH 16
#define ICAT_SINE_WIDTH 16+4
#define ICAT_AMP_WIDTH 16
#define ICAT_FIFO_GATE_HI_LO_SELECT 4
#define ICAT_FIFO_GATE_WIDTH 8
#define ICAT_FIFO_GATE_IOSTANDARD LVCMOS33 LVCMOS33 LVCMOS33 LVCMOS33 LVCMOS33 LVCMOS33 LVCMOS33 LVCMOS33
#define ICAT_DELAY_IOSTANDARD LVCMOS33 LVCMOS33 LVCMOS33 LVCMOS33 LVCMOS33 LVCMOS33 LVCMOS33
#define ICAT_DATA_WIDTH 24
#define ICAT_SPI_DATA_WIDTH 16
#define ICAT_SPI_ADDRESS_WIDTH 14
#define ICAT_TEMP_AVG_WINDOW 16
#define ICAT_TEMP_INT_WIDTH 8
#define ICAT_TEMP_FRAC_WIDTH 8
#define ICAT_TEMP_COMP_TABLE_NAMES phasetc amptc delaytc sinegaintc ampgaintc ampnulltc fgdtc
#define ICAT_TEMP_COMP_TABLE_PHASETC_DEFAULT 0
#define ICAT_TEMP_COMP_TABLE_AMPTC_DEFAULT 32768
#define ICAT_TEMP_COMP_TABLE_DELAYTC_DEFAULT 0
#define ICAT_TEMP_COMP_TABLE_SINEGAINTC_DEFAULT 32768
#define ICAT_TEMP_COMP_TABLE_AMPGAINTC_DEFAULT 32768
#define ICAT_TEMP_COMP_TABLE_AMPNULLTC_DEFAULT 0
#define ICAT_TEMP_COMP_TABLE_FGDTC_DEFAULT 0
#define ICAT_TEMP_COMP_TABLE_PHASETC_POSITION 1
#define ICAT_TEMP_COMP_TABLE_AMPTC_POSITION 0
#define ICAT_TEMP_COMP_TABLE_DELAYTC_POSITION 2
#define ICAT_TEMP_COMP_TABLE_SINEGAINTC_POSITION 5
#define ICAT_TEMP_COMP_TABLE_AMPGAINTC_POSITION 3
#define ICAT_TEMP_COMP_TABLE_AMPNULLTC_POSITION 4
#define ICAT_TEMP_COMP_TABLE_FGDTC_POSITION 6
#define ICAT_NUM_TEMP_COMP_TABLES 16
#define ICAT_TEMP_COMP_TABLE_DEPTH 512
#define ICAT_TEMP_COMP_TABLE_BUS_DEPTH 512*16
#define ICAT_NUM_TEST_POINTS 24
#define ICAT_NUM_MICTOR_IO 32
#define ICAT_TRANSCEIVER_ENABLE_WIDTH 6
#define ICAT_PRIMARY_CLK_IOSTANDARD LVDS_33
#define ICAT_SECONDARY_CLK_IOSTANDARD LVCMOS33
