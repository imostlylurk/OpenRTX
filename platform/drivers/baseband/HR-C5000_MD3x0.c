/***************************************************************************
 *   Copyright (C) 2020 by Federico Amedeo Izzo IU2NUO,                    *
 *                         Niccolò Izzo IU2KIN                             *
 *                         Frederik Saraci IU2NRO                          *
 *                         Silvano Seva IU2KWO                             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <http://www.gnu.org/licenses/>   *
 ***************************************************************************/

#include "HR-C5000_MD3x0.h"
#include <hwconfig.h>
#include <gpio.h>
#include <delays.h>
#include <hwconfig.h>
#include <os.h>

const uint8_t initSeq1[] = {0x00, 0x00, 0xFF, 0xB0, 0x00, 0x00, 0x00, 0x00};
const uint8_t initSeq2[] =
{
    0x00, 0x11, 0x80, 0x0A, 0x22, 0x01, 0x00, 0x00, 0x33, 0xEF, 0x00, 0xFF, 0xFF,
    0xFF, 0xF0, 0xF0, 0x10, 0x00, 0x00, 0x07, 0x3B, 0xF8, 0x0E, 0xFD, 0x40, 0xFF,
    0x00, 0x0B, 0x00, 0x00, 0x00, 0x04, 0x0B, 0x00, 0x17, 0x02, 0xFF, 0xE0, 0x28,
    0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
const uint8_t initSeq3[] =
{
    0x01, 0x10, 0x69, 0x69, 0x96, 0x96, 0x96, 0x99, 0x99, 0x99, 0xA5, 0xA5, 0xAA,
    0xAA, 0xCC, 0xCC, 0x00, 0xF0, 0x01, 0xFF, 0x01, 0x0F
};
const uint8_t initSeq4[] = {0x01, 0x30, 0x30, 0x4E, 0x14, 0x1E, 0x1A, 0x30, 0x3D,
                            0x50, 0x07, 0x60};
const uint8_t initSeq5[] = {0x01, 0x40, 0x90, 0x03, 0x01, 0x02, 0x05, 0x07, 0xF0};
const uint8_t initSeq6[] = {0x01, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00};


uint8_t _spiSendRecv(uint8_t value)
{
    gpio_clearPin(DMR_CLK);

    uint8_t incoming = 0;
    uint8_t cnt = 0;

    for(; cnt < 8; cnt++)
    {
        gpio_setPin(DMR_CLK);

        if(value & (0x80 >> cnt))
        {
            gpio_setPin(DMR_MOSI);
        }
        else
        {
            gpio_clearPin(DMR_MOSI);
        }

        delayUs(1);
        gpio_clearPin(DMR_CLK);
        incoming = (incoming << 1) | gpio_readPin(DMR_MISO);
        delayUs(1);
    }

    return incoming;
}

void _writeReg(uint8_t type, uint8_t reg, uint8_t val)
{
    gpio_clearPin(DMR_CS);
    (void) _spiSendRecv(type);
    (void) _spiSendRecv(reg);
    (void) _spiSendRecv(val);
    gpio_setPin(DMR_CS);
}

void _sendSequence(const uint8_t *seq, uint8_t len)
{
    gpio_clearPin(DMR_CS);

    uint8_t i = 0;
    for(; i < len; i++)
    {
        (void) _spiSendRecv(seq[i]);
    }

    gpio_setPin(DMR_CS);
}

OS_ERR e;

void C5000_init()
{
    gpio_setMode(DMR_CS,    OUTPUT);
    gpio_setMode(DMR_CLK,   OUTPUT);
    gpio_setMode(DMR_MOSI,  OUTPUT);
    gpio_setMode(DMR_MISO,  OUTPUT);
    gpio_setMode(DMR_SLEEP, OUTPUT);

    gpio_setPin(DMR_CS);
    gpio_setPin(DMR_SLEEP);

    OSTimeDly(1, OS_OPT_TIME_DLY, &e);
    gpio_clearPin(DMR_SLEEP);
    C5000_dmrMode();
    OSTimeDly(1, OS_OPT_TIME_DLY, &e);
    C5000_fmMode();
}

void C5000_terminate()
{
}

void C5000_dmrMode()
{
    _writeReg(0x00, 0x0A, 0x80);
    _writeReg(0x00, 0x0B, 0x28);
    _writeReg(0x00, 0x0C, 0x33);
    OSTimeDly(1, OS_OPT_TIME_DLY, &e);
    _writeReg(0x00, 0x0A, 0x00);
    _writeReg(0x00, 0xB9, 0x32);
    _writeReg(0x00, 0xBA, 0x22);
    _writeReg(0x00, 0xBB, 0x11);
    _writeReg(0x00, 0x10, 0x4F);
    _writeReg(0x00, 0x40, 0x43);
    _writeReg(0x00, 0x41, 0x40);
    _writeReg(0x00, 0x07, 0x0B);
    _writeReg(0x00, 0x08, 0xB8);
    _writeReg(0x00, 0x09, 0x00);
    _writeReg(0x00, 0x06, 0x21);
    _sendSequence(initSeq1, sizeof(initSeq1));
    _writeReg(0x00, 0x48, 0x00);
    _writeReg(0x00, 0x47, 0x1F);    /* This is 0x7F - freq_adj_mid */
    _sendSequence(initSeq2, sizeof(initSeq2));
    _writeReg(0x00, 0x00, 0x28);
    OSTimeDly(1, OS_OPT_TIME_DLY, &e);

    _writeReg(0x00, 0x14, 0x59);
    _writeReg(0x00, 0x15, 0xF5);
    _writeReg(0x00, 0x16, 0x21);
    _sendSequence(initSeq3, sizeof(initSeq3));
    _sendSequence(initSeq4, sizeof(initSeq4));
    _sendSequence(initSeq5, sizeof(initSeq5));
    _sendSequence(initSeq6, sizeof(initSeq6));
    _writeReg(0x01, 0x52, 0x08);
    _writeReg(0x01, 0x53, 0xEB);
    _writeReg(0x01, 0x54, 0x78);
    _writeReg(0x01, 0x45, 0x1E);
    _writeReg(0x01, 0x37, 0x50);
    _writeReg(0x01, 0x35, 0xFF);
    _writeReg(0x00, 0x39, 0x02);
    _writeReg(0x00, 0x3D, 0x0A);
    _writeReg(0x00, 0x83, 0xFF);
    _writeReg(0x00, 0x87, 0x00);
    _writeReg(0x00, 0x65, 0x0A);
    _writeReg(0x00, 0x1D, 0xFF);
    _writeReg(0x00, 0x1E, 0xF1);
    _writeReg(0x00, 0x1F, 0x10);
    _writeReg(0x00, 0x0D, 0x8C);
    _writeReg(0x00, 0x0E, 0x44);
    _writeReg(0x00, 0x0F, 0xC8);
    _writeReg(0x00, 0x37, 0xC2);
    _writeReg(0x00, 0x25, 0x0E);
    _writeReg(0x00, 0x26, 0xFD);
    _writeReg(0x00, 0x64, 0x00);

    _writeReg(0x01, 0x24, 0x00);
    _writeReg(0x01, 0x25, 0x00);
    _writeReg(0x01, 0x26, 0x00);
    _writeReg(0x01, 0x27, 0x00);
    _writeReg(0x00, 0x81, 0x19);
    _writeReg(0x00, 0x85, 0x00);
}

void C5000_fmMode()
{
    _writeReg(0x00, 0x0A, 0x80);
    _writeReg(0x00, 0x0B, 0x28);
    _writeReg(0x00, 0x0C, 0x33);
    OSTimeDly(1, OS_OPT_TIME_DLY, &e);
    _writeReg(0x00, 0x0A, 0x00);
    _writeReg(0x00, 0xB9, 0x33);
    _writeReg(0x00, 0xBA, 0x22);
    _writeReg(0x00, 0xBB, 0x11);
    _writeReg(0x00, 0x10, 0x80);
    _writeReg(0x00, 0x07, 0x0E);
    _writeReg(0x00, 0x08, 0x10);
    _writeReg(0x00, 0x09, 0x00);
    _sendSequence(initSeq1, sizeof(initSeq1));
    _writeReg(0x00, 0x06, 0x00);
    _sendSequence(initSeq2, sizeof(initSeq2));
    _writeReg(0x00, 0x48, 0x00);
    _writeReg(0x00, 0x47, 0x1F);    /* This is 0x7F - freq_adj_mid */
    _writeReg(0x00, 0x0D, 0x8C);
    _writeReg(0x00, 0x0E, 0x44);
    _writeReg(0x00, 0x0F, 0xC8);
    _writeReg(0x00, 0x25, 0x0E);
    _writeReg(0x00, 0x26, 0xFE);
    _writeReg(0x00, 0x83, 0xFF);
    _writeReg(0x00, 0x87, 0x00);
    _writeReg(0x00, 0x81, 0x00);
    _writeReg(0x00, 0x60, 0x00);

    _writeReg(0x00, 0x00, 0x28);
}

void C5000_activateAnalogTx()
{
    delayMs(51);
    _writeReg(0x00, 0x25, 0x0E);

    delayMs(15);
    _writeReg(0x00, 0x0D, 0x8C);
    _writeReg(0x00, 0x0E, 0x44);
    _writeReg(0x00, 0x0F, 0xC8);
    _writeReg(0x00, 0x25, 0x0E);
    _writeReg(0x00, 0x26, 0xFE);
    _writeReg(0x00, 0x83, 0xFF);
    _writeReg(0x00, 0x87, 0x00);
    _writeReg(0x00, 0x45, 0x32);
    _writeReg(0x00, 0x46, 0x85);
    _writeReg(0x00, 0x04, 0x1F);
    _writeReg(0x00, 0x35, 0x1E);
    _writeReg(0x00, 0x3F, 0x04);
    _writeReg(0x00, 0x34, 0x3C);
    _writeReg(0x00, 0x3E, 0x08);
    _writeReg(0x00, 0x37, 0xC2);
    _writeReg(0x01, 0x50, 0x00);
    _writeReg(0x01, 0x51, 0x00);
    _writeReg(0x00, 0x81, 0x00);
    _writeReg(0x00, 0x60, 0x80);

    delayMs(15);
    _writeReg(0x00, 0x0E, 0x40);

    delayMs(350);
    _writeReg(0x00, 0x0E, 0x44);
}

void C5000_shutdownAnalogTx()
{
    delayMs(50);
    _writeReg(0x00, 0x60, 0x80);
    delayMs(270);
    _writeReg(0x00, 0x0E, 0x44);
    _writeReg(0x00, 0x60, 0x80);
}

void C5000_writeReg(uint8_t reg, uint8_t val)
{
}

bool C5000_spiInUse()
{
    return (gpio_readPin(DMR_CS) == 0) ? true :  false;
}