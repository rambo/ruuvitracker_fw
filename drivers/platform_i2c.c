#include "platform_i2c.h"

rt_error platform_i2c_send_start( unsigned id )
{
	//D_ENTER();
	RT_TIMEOUT_INIT();
	// Wait for bus to become free (or that we are the master)
	while(1)
	{
		//_DEBUG("waiting for bus, status reg value=%x\n", (unsigned int)I2C_GetLastEvent(i2c[id]));
		I2C_CHECK_BERR();
		if (I2C_GetFlagStatus(i2c[id], I2C_FLAG_MSL)) // We are the master and decide what happens on the bus
		{
			break;
		}
		if (!I2C_GetFlagStatus(i2c[id], I2C_FLAG_BUSY)) // Bus is free 
		{
			break;
		}
		RT_TIMEOUT_CHECK( RT_DEFAULT_TIMEOUT );
	}
	//_DEBUG("%s\n", "bus free check passed");

	// Send I2C1 START condition
	I2C_GenerateSTART(i2c[id], ENABLE);
	// Wait for the start generation to complete
	RT_TIMEOUT_REINIT();
	while(!I2C_GetFlagStatus(i2c[id], I2C_FLAG_SB))
	{
		I2C_CHECK_BERR();
		RT_TIMEOUT_CHECK( RT_DEFAULT_TIMEOUT );
	}
	// Make sure there is no leftover NACK bit laying around...
	//_DEBUG("start generated, status reg value=%x\n", (unsigned int)I2C_GetLastEvent(i2c[id]));
	I2C_ClearFlag(i2c[id], I2C_FLAG_AF);
	//_DEBUG("start generated (NACK cleared), status reg value=%x\n", (unsigned int)I2C_GetLastEvent(i2c[id]));
	//D_EXIT();
	return RT_ERR_OK;
}

rt_error platform_i2c_send_stop( unsigned id )
{
	//D_ENTER();
	RT_TIMEOUT_INIT();
	// wait for the bus to be free for us to send that stop
	while(1)
	{
		//_DEBUG("waiting for bus, status reg value=%x\n", (unsigned int)I2C_GetLastEvent(i2c[id]));
		I2C_CHECK_BERR();
		if (I2C_GetFlagStatus(i2c[id], I2C_FLAG_MSL)) // We are the master and decide what happens on the bus
		{
			break;
		}
		if (!I2C_GetFlagStatus(i2c[id], I2C_FLAG_BUSY)) // Bus is free (though we should not be sending STOPs if we are not the master...)
		{
			break;
		}
		RT_TIMEOUT_CHECK( RT_DEFAULT_TIMEOUT );
	}
	//_DEBUG("%s\n", "bus free check passed");

	// Send I2C1 STOP Condition
	I2C_GenerateSTOP(i2c[id], ENABLE);
	// And wait for the STOP condition to complete and the bus to become free
	RT_TIMEOUT_REINIT();
	while(I2C_GetFlagStatus(i2c[id], I2C_FLAG_BUSY))
	{
		//_DEBUG("waiting for bus, status reg value=%x\n", (unsigned int)I2C_GetLastEvent(i2c[id]));
		I2C_CHECK_BERR();
		RT_TIMEOUT_CHECK( RT_DEFAULT_TIMEOUT );
	}
	// Interestingly enough the bit NACK is not cleared by STOP condition even though documentation claims otherwise...
	//_DEBUG("bus freed, status reg value=%x\n", (unsigned int)I2C_GetLastEvent(i2c[id]));
	I2C_ClearFlag(i2c[id], I2C_FLAG_AF);
	//_DEBUG("bus freed (NACK cleared), status reg value=%x\n", (unsigned int)I2C_GetLastEvent(i2c[id]));
	//D_EXIT();
	return RT_ERR_OK;
}

/* Send 7bit address to I2C buss */
/* Adds R/W bit to end of address.(Should not be included in address) */
rt_error platform_i2c_send_address( unsigned id, u16 address, int direction )
{
	//D_ENTER();
	//_DEBUG("address=0x%x\n", address);
	RT_TIMEOUT_INIT();
	// wait for I2C1 EV5 --> Master has acknowledged start condition
	while(SUCCESS != I2C_CheckEvent(i2c[id], I2C_EVENT_MASTER_MODE_SELECT))
	{
		I2C_CHECK_BERR();
		RT_TIMEOUT_CHECK( RT_DEFAULT_TIMEOUT );
	}
	//_DEBUG("%s\n", "I2C_EVENT_MASTER_MODE_SELECT check passed");

	address<<=1; //Shift 7bit address to left by one to leave room for R/W bit
	RT_TIMEOUT_REINIT();

	/* wait for I2C1 EV6, check if
	 * either Slave has acknowledged Master transmitter or
	 * Master receiver mode, depending on the transmission
	 * direction
	 */
	if(direction == PLATFORM_I2C_DIRECTION_TRANSMITTER)
	{
		I2C_Send7bitAddress(i2c[id], address, I2C_Direction_Transmitter);
		while(1)
		{
			//_DEBUG("I2C_Direction_Transmitter, status reg value=%x\n", (unsigned int)I2C_GetLastEvent(i2c[id]));
			I2C_CHECK_BERR();
			RT_TIMEOUT_CHECK( RT_DEFAULT_TIMEOUT );
			// Do not check any othet flags before we have either NACK or ADDR flag up
			if (   !I2C_GetFlagStatus(i2c[id], I2C_FLAG_AF)
				&& !I2C_GetFlagStatus(i2c[id], I2C_FLAG_ADDR))
			{
				continue;
			}
			if (I2C_GetFlagStatus(i2c[id], I2C_FLAG_AF))
			{
				// NACK
				//_DEBUG("NACK for address=0x%x\n", address);
				//D_EXIT();
				return RT_ERR_FAIL;
			}
			if (I2C_GetFlagStatus(i2c[id], I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
			{
				// ACK
				//_DEBUG("ACK for address=0x%x\n", address);
				//D_EXIT();
				return RT_ERR_OK;
			}
		}
	}
	else if(direction == PLATFORM_I2C_DIRECTION_RECEIVER)
	{
		I2C_Send7bitAddress(i2c[id], address, I2C_Direction_Receiver);
		while(1)
		{
			//_DEBUG("I2C_Direction_Receiver, status reg value=%x\n", (unsigned int)I2C_GetLastEvent(i2c[id]));
			I2C_CHECK_BERR();
			RT_TIMEOUT_CHECK( RT_DEFAULT_TIMEOUT );
			// Do not check any othet flags before we have either NACK or ADDR flag up
			if (   !I2C_GetFlagStatus(i2c[id], I2C_FLAG_AF)
				&& !I2C_GetFlagStatus(i2c[id], I2C_FLAG_ADDR))
			{
				continue;
			}
			if (I2C_GetFlagStatus(i2c[id], I2C_FLAG_AF))
			{
				// NACK
				//_DEBUG("NACK for address=0x%x\n", (uint8_t)(address | 0x1));
				//D_EXIT();
				return RT_ERR_FAIL;
			}
			if (I2C_GetFlagStatus(i2c[id], I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED))
			{
				// ACK
				//_DEBUG("ACK for address=0x%x\n", (uint8_t)(address | 0x1));
				//D_EXIT();
				return RT_ERR_OK;
			}
		}
	}
	// We should not fall this far
	//D_EXIT();
	return RT_ERR_ERROR;
}

/* Send one byte to I2C bus */
rt_error platform_i2c_send_byte( unsigned id, u8 data )
{
	//D_ENTER();
	I2C_SendData(i2c[id], data);
	RT_TIMEOUT_INIT();
	// wait for I2C1 EV8_2 --> byte has been transmitted
	while(!I2C_CheckEvent(i2c[id], I2C_EVENT_MASTER_BYTE_TRANSMITTED))
	{
		//_DEBUG("waiting for byte to be sent, status reg value=%x\n", (unsigned int)I2C_GetLastEvent(i2c[id]));
		I2C_CHECK_BERR();
		RT_TIMEOUT_CHECK( RT_DEFAULT_TIMEOUT );
	}
	//_DEBUG("byte to sent, status reg value=%x\n", (unsigned int)I2C_GetLastEvent(i2c[id]));
	//D_EXIT();
	return RT_ERR_OK;
}

/* This function reads one byte from the slave device */
rt_error platform_i2c_recv_byte( unsigned id, int ack, u8 *buff)
{
	//D_ENTER();
	if(ack) // enable acknowledge of recieved data
	{
		I2C_AcknowledgeConfig(i2c[id], ENABLE);
	}
	else // disabe acknowledge of received data
	{
		I2C_AcknowledgeConfig(i2c[id], DISABLE);
	}
	RT_TIMEOUT_INIT();
	// wait until one byte has been received
	while( !I2C_CheckEvent(i2c[id], I2C_EVENT_MASTER_BYTE_RECEIVED) )
	{
		//_DEBUG("waiting for byte to be received, status reg value=%x\n", (unsigned int)I2C_GetLastEvent(i2c[id]));
		I2C_CHECK_BERR();
		RT_TIMEOUT_CHECK( RT_DEFAULT_TIMEOUT );
	}
	// read data from I2C data register and return data byte
	*buff = I2C_ReceiveData(i2c[id]);
	//D_EXIT();
	return RT_ERR_OK;
}

static rt_error _i2c_recv_buf(unsigned id, u8 *buff, int len, int *recv_count)
{
	int i, ack;
	rt_error status;
	for (i=0;i<len;i++)
	{
		if (i<(len-1))
			ack = 1;
		else
			ack = 0;
		status = platform_i2c_recv_byte(id, ack, &buff[i]);
		if (status != RT_ERR_OK)
			return status;
	}
	*recv_count = i;
	return RT_ERR_OK;
}

static rt_error _i2c_send_buf(unsigned id, u8 *buff, int len, int *send_count)
{
	int i;
	rt_error status;
	for (i=0;i<len;i++)
	{
		status = platform_i2c_send_byte(id, buff[i]);
		if (status != RT_ERR_OK)
			return status;
	}
	*send_count = i;
	return RT_ERR_OK;
}
