/** @file cbl_cmds_memory.c
 *
 * @brief Contains functions for memory access from the bootloader
 */
#include "cbl_cmds_memory.h"
#include "string.h"

static cbl_err_code_t write_get_params (parser_t * ph_prsr, uint32_t * p_start,
        uint32_t * p_len, cksum_t * cksum);

/**
 * @brief   Jumps to a requested address.
 *          Parameters needed from phPrsr:
 *              - address
 */
cbl_err_code_t cmd_jump_to (parser_t * phPrsr)
{
    cbl_err_code_t eCode = CBL_ERR_OK;
    char *charAddr;
    uint32_t addr = 0u;
    void (*jump) (void);

    DEBUG("Started\r\n");

    /* Get the address in hex form */
    charAddr = parser_get_val(phPrsr, TXT_PAR_JUMP_TO_ADDR,
            strlen(TXT_PAR_JUMP_TO_ADDR));
    if (NULL == charAddr)
    {
        return CBL_ERR_NEED_PARAM;
    }
    /* Fill addr, skips 0x if present */
    eCode = str2ui32(charAddr, strlen(charAddr), &addr, 16u);
    ERR_CHECK(eCode);

    /* Make sure we can jump to the wanted location */
    eCode = verify_jump_address(addr);
    ERR_CHECK(eCode);

    /* Add 1 to the address to set the T bit */
    addr++;
    /*!<    T bit is 0th bit of a function address and tells the processor
     *  if command is ARM T=0 or thumb T=1. STM uses thumb commands.
     *  Reference: https://www.youtube.com/watch?v=VX_12SjnNhY */

    /* Make a function to jump to */
    jump = (void *)addr;

    /* Send response */
    eCode = send_to_host(TXT_SUCCESS, strlen(TXT_SUCCESS));
    ERR_CHECK(eCode);

    /* Jump to requested address, user ensures requested address is valid */
    jump();
    return eCode;
}

// \f - new page
/**
 * @brief   Erases flash memory according to parameters.
 *          Parameters needed from phPrsr:
 *              - type - Defines type of flash erase. "mass" erases all sectors,
 *               "sector" erases only selected sectors
 *              - sector - First sector to erase. Bootloader is on sectors 0, 1
 *               and 2. Not needed with mass erase
 *              - count - Number of sectors to erase. Not needed with mass erase
 */
cbl_err_code_t cmd_flash_erase (parser_t * phPrsr)
{
    cbl_err_code_t eCode = CBL_ERR_OK;
    char *charSect = NULL;
    char *charCount = NULL;
    char *type = NULL;
    uint32_t sect;
    uint32_t count;

    DEBUG("Started\r\n");

    type = parser_get_val(phPrsr, TXT_PAR_FLASH_ERASE_TYPE,
            strlen(TXT_PAR_FLASH_ERASE_TYPE));
    if (NULL == type)
    {
        /* No sector present throw error */
        return CBL_ERR_NEED_PARAM;
    }
    /* Check the type of erase */
    if (strncmp(type, TXT_PAR_FLASH_ERASE_TYPE_SECT,
            strlen(TXT_PAR_FLASH_ERASE_TYPE_SECT)) == 0)
    {
        /* Get first sector to write to */
        charSect = parser_get_val(phPrsr, TXT_PAR_FLASH_ERASE_SECT,
                strlen(TXT_PAR_FLASH_ERASE_SECT));
        if (NULL == charSect)
        {
            /* No sector present throw error */
            return CBL_ERR_NEED_PARAM;
        }

        /* Fill sect */
        eCode = str2ui32(charSect, strlen(charSect), &sect, 10);
        ERR_CHECK(eCode);

        /* Get how many sectors to erase */
        charCount = parser_get_val(phPrsr, TXT_PAR_FLASH_ERASE_COUNT,
                strlen(TXT_PAR_FLASH_ERASE_COUNT));
        if (NULL == charCount)
        {
            /* No sector count present throw error */
            return CBL_ERR_NEED_PARAM;
        }

        /* Fill count */
        eCode = str2ui32(charCount, strlen(charCount), &count, 10);
        ERR_CHECK(eCode);

        eCode = flash_erase_sector(sect, count);
        ERR_CHECK(eCode);
    }
    else if (strncmp(type, TXT_PAR_FLASH_ERASE_TYPE_MASS,
            strlen(TXT_PAR_FLASH_ERASE_TYPE_MASS)) == 0)
    {
        eCode = flash_erase_mass();
        ERR_CHECK(eCode);
    }
    else
    {
        /* Type has wrong value */
        return CBL_ERR_ERASE_INV_TYPE;
    }

    /* Send response */
    eCode = send_to_host(TXT_SUCCESS, strlen(TXT_SUCCESS));

    return eCode;
}

// \f - new page
/**
 * @brief   Writes to flash, sector to be written into shall be erased prior
 *          Parameters needed from phPrsr:
 *             - start - Starting address in hex format (e.g. 0x12345678),
 *               0x can be omitted
 *             - count - Number of bytes to write without checksum.
 *                       Maximum bytes: FLASH_WRITE_SZ
 *             - cksum - Checksum to use
 *
 * @note    If using checksum, data will be written to memory before checking
 *          for checksum!
 */
cbl_err_code_t cmd_flash_write (parser_t * phPrsr)
{
    cbl_err_code_t eCode = CBL_ERR_OK;
    uint32_t start;
    uint32_t len;
    cksum_t cksum = CKSUM_UNDEF;

    DEBUG("Started\r\n");

    eCode = write_get_params(phPrsr, &start, &len, &cksum);
    ERR_CHECK(eCode);

    eCode = flash_write(start, len, cksum);
    ERR_CHECK(eCode);

    eCode = send_to_host(TXT_SUCCESS, strlen(TXT_SUCCESS));

    return eCode;
}

// \f - new page
/**
 * @brief   Read bytes from memory
 *          Parameters needed from phPrsr:
 *             - start - Starting address in hex format (e.g. 0x12345678),
 *              0x can be omitted
 *             - count - Number of bytes to read
 */
cbl_err_code_t cmd_mem_read (parser_t * phPrsr)
{
    cbl_err_code_t eCode = CBL_ERR_OK;
    char *charStart = NULL;
    char *charLen = NULL;
    uint32_t start;
    uint32_t len;

    DEBUG("Started\r\n");

    /* Get starting address */
    charStart = parser_get_val(phPrsr, TXT_PAR_FLASH_WRITE_START,
            strlen(TXT_PAR_FLASH_WRITE_START));
    if (NULL == charStart)
    {
        return CBL_ERR_NEED_PARAM;
    }
    /* Get length in bytes */
    charLen = parser_get_val(phPrsr, TXT_PAR_FLASH_WRITE_COUNT,
            strlen(TXT_PAR_FLASH_WRITE_COUNT));
    if (NULL == charLen)
    {
        return CBL_ERR_NEED_PARAM;
    }
    /* Fill start */
    eCode = str2ui32(charStart, strlen(charStart), &start, 16);
    ERR_CHECK(eCode);

    /* Fill len */
    eCode = str2ui32(charLen, strlen(charLen), &len, 10);
    ERR_CHECK(eCode);

    /* Send requested bytes */
    eCode = send_to_host((char *)start, len);
    return eCode;
}

/**
 * @brief Erase flash sectors
 *
 * @param sect Initial sector to erase
 * @param count Number of sectors to erase
 */
cbl_err_code_t flash_erase_sector (uint32_t sect, uint32_t count)
{
    cbl_err_code_t eCode = CBL_ERR_OK;
    FLASH_EraseInitTypeDef settings;
    HAL_StatusTypeDef HALCode;
    uint32_t sectorCode;

    /* Check validity of given sector */
    if (sect >= FLASH_SECTOR_TOTAL)
    {
        return CBL_ERR_INV_SECT;
    }

    if (sect + count - 1 >= FLASH_SECTOR_TOTAL)
    {
        /* Last sector to delete doesn't exist, throw error */
        return CBL_ERR_INV_SECT_COUNT;
    }

    /* Device operating range: 2.7V to 3.6V */
    settings.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    /* Only available bank */
    settings.Banks = FLASH_BANK_1;
    settings.TypeErase = FLASH_TYPEERASE_SECTORS;
    settings.Sector = sect;
    settings.NbSectors = count;

    /* Turn on the blue LED, signalizing flash manipulation */
    LED_ON(BLUE);

    /* Unlock flash control registers */
    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return CBL_ERR_HAL_UNLOCK;
    }

    /* Erase selected sectors */
    HALCode = HAL_FLASHEx_Erase( &settings, &sectorCode);

    LED_OFF(BLUE);

    /* Lock flash control registers */
    HAL_FLASH_Lock();

    /* Check for errors */
    if (HALCode != HAL_OK)
    {
        return CBL_ERR_HAL_ERASE;
    }
    if (sectorCode != 0xFFFFFFFFU) /*!< 0xFFFFFFFFU means success */
    {
        /* Shouldn't happen as we check for HALCode before, but let's check */
        return CBL_ERR_SECTOR;
    }

    return eCode;
}

/**
 * @brief Erase whole flash
 */
cbl_err_code_t flash_erase_mass (void)
{
    cbl_err_code_t eCode = CBL_ERR_OK;
    FLASH_EraseInitTypeDef settings;
    HAL_StatusTypeDef HALCode;
    uint32_t sectorCode;

    /* Device operating range: 2.7V to 3.6V */
    settings.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    /* Only available bank */
    settings.Banks = FLASH_BANK_1;

    /* Erase all sectors */
    settings.TypeErase = FLASH_TYPEERASE_MASSERASE;

    /* Turn on the blue LED, signalizing flash manipulation */
    LED_ON(BLUE);

    /* Unlock flash control registers */
    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return CBL_ERR_HAL_UNLOCK;
    }

    /* Erase selected sectors */
    HALCode = HAL_FLASHEx_Erase( &settings, &sectorCode);

    LED_OFF(BLUE);

    /* Lock flash control registers */
    HAL_FLASH_Lock();

    /* Check for errors */
    if (HALCode != HAL_OK)
    {
        return CBL_ERR_HAL_ERASE;
    }

    if (sectorCode != 0xFFFFFFFFU) /*!< 0xFFFFFFFFU means success */
    {
        /* Shouldn't happen as we check for HALCode before, but let's check */
        return CBL_ERR_SECTOR;
    }

    return eCode;
}

/**
 * @brief  Writes to flash, sector to be written into shall be erased prior
 *
 * @param start Starting address
 * @param len   Number of bytes to write without checksum.
 * @param cksum Checksum to use
 *
 * @note    If using checksum, data will be written to memory before checking
 *          for checksum!
 */
cbl_err_code_t flash_write (uint32_t start, uint32_t len, cksum_t cksum)
{
    cbl_err_code_t eCode = CBL_ERR_OK;
    uint8_t write_buf[FLASH_WRITE_SZ] = { 0 };
    uint32_t n_chunks;
    uint32_t iii = 0;
    uint32_t left_to_write;
    uint32_t chunk_addr;
    char chunk_succ[] = "\r\nchunk OK\r\n";
    SHA256_CTX h_cksum_sha256 = { 0 };
    char chunk_info[64] = { 0 };
    uint32_t cksum_len = 0;

    /* Get number of chunks */
    n_chunks = len / FLASH_WRITE_SZ;
    n_chunks = len % FLASH_WRITE_SZ ? n_chunks + 1 : n_chunks;

    /* Notify host how many chunks are expected */
    snprintf(chunk_info, sizeof(chunk_info), "\r\nchunks:%lu\r\n", n_chunks);
    eCode = send_to_host(chunk_info, strlen(chunk_info));
    ERR_CHECK(eCode);

    left_to_write = len;
    chunk_addr = start;

    /* Second parameter is used only when sha256 is used */
    init_checksum(cksum, &h_cksum_sha256);

    /* Get chunks one by one from host, and write them to memory, accumulating
     * checksum */
    while (iii < n_chunks)
    {
        uint32_t chunk_len = ui32_min(left_to_write, (uint32_t)FLASH_WRITE_SZ);

        /* Notify host about current chunk number and length */
        snprintf(chunk_info, sizeof(chunk_info),
                "\r\nchunk:%lu|length:%lu|address:0x%08lx\r\n", iii, chunk_len,
                chunk_addr);
        eCode = send_to_host(chunk_info, strlen(chunk_info));
        ERR_CHECK(eCode);

        /* Reset UART byte counter */
        gRxCmdCntr = 0;

        /* Notify host to send the bytes */
        eCode = send_to_host(TXT_RESP_FLASH_WRITE_READY,
                strlen(TXT_RESP_FLASH_WRITE_READY));
        ERR_CHECK(eCode);

        /* Request 'chunk_len' bytes */
        eCode = recv_from_host_start(write_buf, chunk_len);
        ERR_CHECK(eCode);


        while (gRxCmdCntr != 1)
        {
            /* Wait for 'len' bytes */
        }

        LED_ON(BLUE);
        eCode = write_program_bytes(chunk_addr, write_buf, chunk_len);
        LED_OFF(BLUE);
        ERR_CHECK(eCode);

        /* NOTE: Last parameter is used only when sha256 is used */
        accumulate_checksum(write_buf, chunk_len, cksum, &h_cksum_sha256);

        eCode = send_to_host(chunk_succ, strlen(chunk_succ));
        ERR_CHECK(eCode);

        chunk_addr += chunk_len;
        left_to_write -= chunk_len;
        iii++;
    }

    if (cksum != CKSUM_NO)
    {
        cksum_len = checksum_get_length(cksum);

        /* Notify host cksum is expected */
        snprintf(chunk_info, sizeof(chunk_info), "\r\nchecksum|length:%lu\r\n",
                cksum_len);
        eCode = send_to_host(chunk_info, strlen(chunk_info));
        ERR_CHECK(eCode);

        /* Reset UART byte counter */
        gRxCmdCntr = 0;

        /* Notify host to send the bytes */
        eCode = send_to_host(TXT_RESP_FLASH_WRITE_READY,
                strlen(TXT_RESP_FLASH_WRITE_READY));
        ERR_CHECK(eCode);

        /* Request 'chunk_len' bytes */
        eCode = recv_from_host_start(write_buf, cksum_len);
        ERR_CHECK(eCode);

        while (gRxCmdCntr != 1)
        {
            /* Wait for 'cksum_len' bytes */
        }

        eCode = verify_checksum(write_buf, cksum_len, cksum, &h_cksum_sha256);
        ERR_CHECK(eCode);
    }
    return eCode;
}

/**
 * @brief Uses HAL level commands to write data to memory
 * @param addr Starting address to write bytes to
 * @param data Array of bytes to be writen
 * @param len length of 'data'
 */
cbl_err_code_t write_program_bytes (uint32_t addr, uint8_t * data, uint32_t len)
{
    HAL_StatusTypeDef HALCode;

    /* Unlock flash */
    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return CBL_ERR_HAL_UNLOCK;
    }

    /* Write to flash */
    for (uint32_t iii = 0u; iii < len; iii++)
    {
        /* Write a byte */
        HALCode = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, addr + iii,
                data[iii]);
        if (HALCode != HAL_OK)
        {
            HAL_FLASH_Lock();
            return CBL_ERR_HAL_WRITE;
        }
    }

    HAL_FLASH_Lock();
    return CBL_ERR_OK;
}

/**
 * @brief Gets parameters from parser handle
 *
 * @param ph_prsr[in] Parser containing parameters
 * @param p_start[out] Pointer of start address
 * @param p_len[out] Pointer to length to write
 * @param p_cksum[out] Pointer to checksum enumerator
 */
static cbl_err_code_t write_get_params (parser_t * ph_prsr, uint32_t * p_start,
        uint32_t * p_len, cksum_t * p_cksum)
{
    cbl_err_code_t eCode = CBL_ERR_OK;
    char *charStart = NULL;
    char *charLen = NULL;
    char *charChecksum = NULL;

    /* Get starting address */
    charStart = parser_get_val(ph_prsr, TXT_PAR_FLASH_WRITE_START,
            strlen(TXT_PAR_FLASH_WRITE_START));
    if (NULL == charStart)
    {
        return CBL_ERR_NEED_PARAM;
    }
    /* Get length in bytes */
    charLen = parser_get_val(ph_prsr, TXT_PAR_FLASH_WRITE_COUNT,
            strlen(TXT_PAR_FLASH_WRITE_COUNT));
    if (NULL == charLen)
    {
        return CBL_ERR_NEED_PARAM;
    }

    /* Get checksum to be used */
    charChecksum = parser_get_val(ph_prsr, TXT_PAR_CKSUM,
            strlen(TXT_PAR_CKSUM));
    /* This is an optional parameter, if it is not present, don't throw error */

    /* Fill start */
    eCode = str2ui32(charStart, strlen(charStart), p_start, 16);
    ERR_CHECK(eCode);

    /* Fill len */
    eCode = str2ui32(charLen, strlen(charLen), p_len, 10);
    ERR_CHECK(eCode);

    if (IS_FLASH_ADDRESS(*p_start) == false
            || IS_FLASH_ADDRESS((*p_start) + (*p_len) - 1) == false)
    {
        return CBL_ERR_WRITE_INV_ADDR;
    }

    eCode = enum_checksum(charChecksum, strlen(charChecksum), p_cksum);
    ERR_CHECK(eCode);

    if ((( *p_len) == 0) || (( *p_cksum == CKSUM_CRC32) && ( *p_len % 4 != 0)))
    {
        return CBL_ERR_CRC_LEN;
    }

    return eCode;
}

/*** end of file ***/