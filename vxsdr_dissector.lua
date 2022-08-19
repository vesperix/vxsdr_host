local vxsdr = Proto("vxsdr", "VXSDR Protocol")

local vxsdr_port = 1030
local vxsdr_min_len = 8

local packet_types = {
    [0x00] = "TX_SIGNAL_DATA",
    [0x01] = "RX_SIGNAL_DATA",
    [0x02] = "DEVICE_CMD",
    [0x03] = "TX_RADIO_CMD",
    [0x04] = "RX_RADIO_CMD",
    [0x05] = "ASYNC_MSG",
    [0x22] = "DEVICE_CMD_ERR",
    [0x23] = "TX_RADIO_CMD_ERR",
    [0x24] = "RX_RADIO_CMD_ERR",
    [0x12] = "DEVICE_CMD_RSP",
    [0x13] = "TX_RADIO_CMD_RSP",
    [0x34] = "RX_RADIO_CMD_RSP",
    [0x30] = "TX_SIGNAL_DATA_ACK",
    [0x31] = "RX_SIGNAL_DATA_ACK",
    [0x32] = "DEVICE_CMD_ACK",
    [0x33] = "TX_RADIO_CMD_ACK",
    [0x34] = "RX_RADIO_CMD_ACK",
}

local packet_types_index = {}
for k, v in pairs(packet_types) do
    packet_types_index[v] = k
end

local device_cmds = {
    [0x00] = "HELLO",
    [0x01] = "SET_TIME_NOW",
    [0x02] = "SET_TIME_NEXT_PPS",
    [0x03] = "GET_TIME",
    [0x04] = "GET_STATUS",
    [0x05] = "CLEAR_STATUS",
    [0x06] = "GET_BUFFER_INFO",
    [0x07] = "GET_BUFFER_USE",
    [0x08] = "GET_STREAM_STATE",
    [0x09] = "STOP",
    [0x0A] = "GET_TRANSPORT_INFO", -- unimplemented
    [0x0B] = "GET_TRANSPORT_ADDR",
    [0x0C] = "GET_MAX_PAYLOAD",
    [0x0D] = "CLEAR_DATA_BUFFER",
    [0x0E] = "DISCOVER",
    [0x0F] = "SET_TRANSPORT_ADDR",
    [0x10] = "SET_MAX_PAYLOAD",
    [0x11] = "SAVE_TRANSPORT_ADDR",
    [0x12] = "GET_CHANNEL_INFO",  -- unimplemented
    [0x13] = "GET_TIMING_INFO",   -- unimplemented
    [0x14] = "GET_TIMING_STATUS",
    [0x15] = "GET_TIMING_REF",  -- unimplemented
    [0x16] = "SET_TIMING_REF",  -- unimplemented
    [0x17] = "GET_TIMING_RESOLUTION",
    [0x18] = "GET_NUM_SENSORS",
    [0x19] = "GET_SENSOR",
    [0x20] = "GET_SENSOR_NAME"
    -- 0x21-0x2F reserved
    [0x30] = "READ_DAC_REG",
    [0x31] = "WRITE_DAC_REG",
    [0x32] = "READ_ADC_REG",
    [0x33] = "WRITE_ADC_REG",
    [0x3C] = "APP_UPDATE_MODE_SET",
    [0x3D] = "APP_UPDATE_DATA",
    [0x3E] = "APP_UPDATE_DONE",
    [0x3F] = "RESET"  -- unimplemented
}

local device_cmd_rsps = device_cmds

local device_cmds_index = {}
for k, v in pairs(device_cmds) do
    device_cmds_index[v] = k
end

local device_cmd_rsps_index = {}
for k, v in pairs(device_cmd_rsps) do
    device_cmd_rsps_index[v] = k
end

local device_cmd_errs = {
    [0x00] = "NO_ERROR",
    [0x01] = "BAD_CMD",
    [0x02] = "BUSY",
    [0x03] = "NO_SUCH_SUBDEVICE",
    [0x04] = "NO_SUCH_CHANNEL",
    [0x05] = "TIMEOUT",
    [0x06] = "BAD_HEADER_SIZE",
    [0x07] = "BAD_HEADER_FLAGS",
    [0x08] = "BAD_PARAMETER",
    [0x09] = "NOT_SUPPORTED",
    [0x0A] = "BAD_PKT_SIZE",
    [0x0B] = "FAILED"
}

local async_msgs = {
    [0x00] = "NO_ERROR",
    [0x01] = "DATA_UNDERFLOW",
    [0x02] = "DATA_OVERFLOW",
    [0x03] = "OVER_TEMP",
    [0x04] = "POWER_ERROR",
    [0x05] = "FREQ_ERROR",
    [0x06] = "OUT_OF_SEQUENCE",
    [0x07] = "CTRL_OVERFLOW",
    [0x08] = "PPS_TIMEOUT"
}

local radio_cmds = {
    [0x01] = "START_NOW_UNTIL_STOP",
    [0x02] = "START_AT_TIME_UNTIL_STOP",
    [0x03] = "STOP_NOW",
    [0x04] = "START_NOW_FOR_N_SAMPLES",
    [0x05] = "START_AT_TIME_FOR_N_SAMPLES",
    [0x06] = "LOOP_AT_TIME_FOR_N_SAMPLES",
    [0x07] = "GET_RF_FREQ",
    [0x08] = "GET_RF_GAIN",
    [0x09] = "GET_SAMPLE_RATE",
    [0x0A] = "GET_RF_BW",  -- unimplemented
    [0x0B] = "GET_RF_ENABLED",
    [0x0C] = "GET_RF_PORT",
    [0x0D] = "GET_NUM_RF_PORTS",
    -- skipped 0x0E
    [0x0F] = "GET_RF_PORT_NAME",
    [0x10] = "GET_LO_INPUT", --unimplemented
    [0x11] = "GET_LOCK_STATUS",
    [0x12] = "GET_MASTER_CLK",
    [0x13] = "SET_RF_FREQ",
    [0x14] = "SET_RF_GAIN",
    [0x15] = "SET_SAMPLE_RATE",
    [0x16] = "SET_RF_BW",  -- unimplemented
    [0x17] = "SET_RF_ENABLED",
    [0x18] = "SET_RF_PORT",
    [0x19] = "SET_RF_PORT_BY_NAME",
    [0x1A] = "SET_LO_INPUT", -- unimplemented
    [0x1B] = "SET_MASTER_CLK", --unimplemented
    [0x1C] = "GET_NUM_SUBDEVS",
    [0x1D] = "GET_RF_FREQ_RANGE",
    [0x1E] = "GET_RF_GAIN_RANGE",
    [0x1F] = "GET_SAMPLE_RATE_RANGE",
    [0x20] = "GET_NUM_CHANNELS",
    [0x21] = "SET_ADC_TEST_PATTERN",
    [0x22] = "GET_IQ_BIAS",
    [0x23] = "GET_IQ_CORR",
    [0x24] = "SET_IQ_BIAS",
    [0x25] = "SET_IQ_CORR",
    [0x26] = "DATA_LOOP"
    -- 0x53 - 0xFF reserved
}

local radio_cmds_index = {}
for k, v in pairs(radio_cmds) do
    radio_cmds_index[v] = k
end

local radio_cmd_rsps = radio_cmds

local radio_cmd_rsps_index = {}
for k, v in pairs(radio_cmd_rsps) do
    radio_cmd_rsps_index[v] = k
end

local radio_cmd_errs = {
    [0x01] = "BAD_CMD",
    [0x02] = "BUSY",
    [0x03] = "NO_SUCH_SUBDEVICE",
    [0x04] = "TIMEOUT",
    [0x05] = "BAD_PARAMETER",
    [0x06] = "NOT_SUPPORTED",
    [0x07] = "BAD_PKT_SIZE"
}

local pf_packet_type       = ProtoField.uint8("vxsdr.packet_type", "Packet Type", base.HEX, packet_types)
local pf_command           = ProtoField.uint8("vxsdr.command", "Command", base.HEX)
local pf_target_subdevice  = ProtoField.uint8("vxsdr.target_subdevice", "Target Subdevice", base.DEC)
local pf_flags             = ProtoField.uint8("vxsdr.flags", "Flags", base.HEX)
local pf_packet_size       = ProtoField.uint16("vxsdr.packet_size", "Packet Size", base.DEC)
local pf_sequence_counter  = ProtoField.uint16("vxsdr.sequence_counter", "Sequence Counter", base.DEC)

local pf_device_command     = ProtoField.uint8("vxsdr.device_command", "Device Command", base.HEX, device_cmds)
local pf_device_command_err = ProtoField.uint8("vxsdr.device_command_err", "Device Command Error", base.HEX, device_cmds)
local pf_device_command_rsp = ProtoField.uint8("vxsdr.device_command_rsp", "Device Command Response", base.HEX, device_cmds)
local pf_radio_command      = ProtoField.uint8("vxsdr.radio_command", "Radio Command", base.HEX, radio_cmds)
local pf_radio_command_err  = ProtoField.uint8("vxsdr.radio_command_err", "Radio Command Error", base.HEX, radio_cmds)
local pf_radio_command_rsp  = ProtoField.uint8("vxsdr.radio_command_rsp", "Radio Command Response", base.HEX, radio_cmds)
local pf_device_error       = ProtoField.uint32("vxsdr.device_error", "Device Error Reason", base.HEX, device_cmd_errs)
local pf_radio_error        = ProtoField.uint32("vxsdr.radio_error", "Radio Error Reason", base.HEX, radio_cmd_errs)
local pf_async_msg          = ProtoField.uint8("vxsdr.async_msg", "Async Message", base.HEX, async_msgs)

local pf_uint16_data        = ProtoField.uint16("vxsdr.data", "UInt16 Data", base.DEC)
local pf_uint32_data        = ProtoField.uint32("vxsdr.data", "UInt32 Data", base.DEC)
local pf_uint64_data        = ProtoField.uint64("vxsdr.data", "UInt64 Data", base.DEC)
local pf_double_data        = ProtoField.double("vxsdr.data", "Double Data")

vxsdr.fields = { pf_packet_type, pf_command, pf_target_subdevice, pf_flags, pf_packet_size, pf_sequence_counter,
                 pf_device_command, pf_radio_command, pf_device_command_err, pf_radio_command_err,
                 pf_device_command_rsp, pf_radio_command_rsp, pf_device_error, pf_radio_error, pf_async_msg,
                 pf_uint16_data, pf_uint32_data, pf_uint64_data, pf_double_data }

function vxsdr.dissector(tvbuf, pktinfo, root)

    pktinfo.cols.protocol:set("VXSDR")
    local pkt_len = tvbuf:reported_length_remaining()
    local pkt_type = tvbuf:range(0, 1):uint()
    local pkt_cmd  = tvbuf:range(1, 1):uint()
    local pkt_size = tvbuf:range(4, 2):uint()

    local tree = root:add(vxsdr, tvbuf:range(0, pktlen))

    if pkt_len < vxsdr_min_len then
        return
    end

    tree:add(   pf_packet_type,      tvbuf:range(0, 1))
    if pkt_type == packet_types_index["DEVICE_CMD"] then
        tree:add(   pf_device_command,         tvbuf:range(1, 1))
    elseif pkt_type == packet_types_index["DEVICE_CMD_RSP"] then
        tree:add(   pf_device_command_rsp,     tvbuf:range(1, 1))
    elseif pkt_type == packet_types_index["DEVICE_CMD_ERR"] then
        tree:add(   pf_device_command_err,         tvbuf:range(1, 1))
    elseif pkt_type == packet_types_index["TX_RADIO_CMD"] or pkt_type ==packet_types_index["RX_RADIO_CMD"] then
        tree:add(   pf_radio_command,          tvbuf:range(1, 1))
    elseif pkt_type == packet_types_index["TX_RADIO_CMD_RSP"] or pkt_type ==packet_types_index["RX_RADIO_CMD_RSP"] then
        tree:add(   pf_radio_command_rsp,      tvbuf:range(1, 1))
    elseif ppkt_type == packet_types_index["TX_RADIO_CMD_ERR"] or pkt_type ==packet_types_index["RX_RADIO_CMD_ERR"] then
        tree:add(   pf_radio_command_err,          tvbuf:range(1, 1))
    elseif pkt_type == packet_types_index["ASYNC_MSG"] then
        tree:add(   pf_async_msg,              tvbuf:range(1, 1))
    else
        tree:add(   pf_command,                tvbuf:range(1, 1))
    end
    tree:add(   pf_target_subdevice, tvbuf:range(2, 1))
    tree:add(   pf_flags,            tvbuf:range(3, 1))
    tree:add_le(pf_packet_size,      tvbuf:range(4, 2))
    tree:add_le(pf_sequence_counter, tvbuf:range(6, 2))
    if pkt_size > 8 then
        -- device commands
        if pkt_type == packet_types_index["DEVICE_CMD"] then
            -- set time now or next pps
            if pkt_cmd == device_cmds_index["SET_TIME_NOW"] or pkt_cmd == device_cmds_index["SET_TIME_NEXT_PPS"] then
                tree:add_le(pf_uint32_data, tvbuf:range( 8, 4))
                tree:add_le(pf_uint32_data, tvbuf:range(12, 4))
            end
        -- device command responses
        elseif pkt_type == packet_types_index["DEVICE_CMD_RSP"] then
            if pkt_cmd == device_cmd_rsps_index["HELLO"] then
                tree:add_le(pf_uint32_data, tvbuf:range( 8, 4))
                tree:add_le(pf_uint32_data, tvbuf:range(12, 4))
                tree:add_le(pf_uint32_data, tvbuf:range(16, 4))
                tree:add_le(pf_uint32_data, tvbuf:range(20, 4))
            elseif pkt_cmd == device_cmd_rsps_index["GET_TIME"] or
               pkt_cmd == device_cmd_rsps_index["GET_BUFFER_INFO"] or pkt_cmd == device_cmd_rsps_index["GET_BUFFER_USE"] then
                -- get_time, buffer info, buffer use response
                tree:add_le(pf_uint32_data, tvbuf:range( 8, 4))
                tree:add_le(pf_uint32_data, tvbuf:range(12, 4))
            elseif pkt_cmd == device_cmd_rsps_index["GET_STATUS"] then
                -- status response
                tree:add_le(pf_uint32_data, tvbuf:range( 8, 4))
                tree:add_le(pf_uint32_data, tvbuf:range(12, 4))
                tree:add_le(pf_uint32_data, tvbuf:range(16, 4))
                tree:add_le(pf_uint32_data, tvbuf:range(20, 4))
                tree:add_le(pf_uint32_data, tvbuf:range(24, 4))
                tree:add_le(pf_uint32_data, tvbuf:range(28, 4))
            end
        -- device command errors
        elseif pkt_type == packet_types_index["DEVICE_CMD_ERR"] then
            tree:add_le(pf_device_error, tvbuf:range( 8, 4))
            tree:add_le(pf_uint32_data,  tvbuf:range(12, 4))
        -- radio commands
        elseif pkt_type == packet_types_index["TX_RADIO_CMD"] or pkt_type == packet_types_index["RX_RADIO_CMD"] then
            -- set rf_enabled
            if pkt_cmd == radio_cmds_index["SET_RF_ENABLED"] then
                tree:add_le(pf_uint32_data, tvbuf:range(8, 4))
            -- set freq, gain, or rate
            elseif pkt_cmd == radio_cmds_index["SET_RF_FREQ"] or pkt_cmd == radio_cmds_index["SET_RF_GAIN"] or
                   pkt_cmd == radio_cmds_index["SET_SAMPLE_RATE"]  then
                tree:add_le(pf_double_data, tvbuf:range(8, 8))
            -- start at time
            elseif pkt_cmd == radio_cmds_index["START_AT_TIME_UNTIL_STOP"]  then
                tree:add_le(pf_uint32_data, tvbuf:range( 8, 4))
                tree:add_le(pf_uint32_data, tvbuf:range(12, 4))
            -- start now for n samples
            elseif pkt_cmd == pkt_cmd == radio_cmds_index["START_NOW_FOR_N_SAMPLES"]  then
                tree:add_le(pf_uint64_data, tvbuf:range( 8, 8))
            -- start at time for n samples
            elseif pkt_cmd == pkt_cmd == radio_cmds_index["START_AT_TIME_FOR_N_SAMPLES"]  then
                tree:add_le(pf_uint32_data, tvbuf:range( 8, 4))
                tree:add_le(pf_uint32_data, tvbuf:range(12, 4))
                tree:add_le(pf_uint64_data, tvbuf:range(16, 8))
            end
        -- radio command responses
        elseif pkt_type == packet_types_index["TX_RADIO_CMD_RSP"] or pkt_type == packet_types_index["RX_RADIO_CMD_RSP"] then
            -- get rf_enabled
            if pkt_cmd == radio_cmd_rsps_index["GET_RF_ENABLED"] then
                tree:add_le(pf_uint32_data, tvbuf:range(8, 4))
            -- get freq, gain, or rate
            elseif pkt_cmd == radio_cmd_rsps_index["GET_RF_FREQ"] or pkt_cmd == radio_cmd_rsps_index["GET_RF_GAIN"] or
                   pkt_cmd == radio_cmd_rsps_index["GET_SAMPLE_RATE"]  then
                tree:add_le(pf_double_data, tvbuf:range(8, 8))
            elseif pkt_cmd == radio_cmd_rsps_index["GET_RF_FREQ_RANGE"] or pkt_cmd == radio_cmd_rsps_index["GET_RF_GAIN_RANGE"] or
                   pkt_cmd == radio_cmd_rsps_index["GET_RATE_RANGE"]  then
                tree:add_le(pf_double_data, tvbuf:range(8, 8))
                tree:add_le(pf_double_data, tvbuf:range(16, 8))
            end
        -- radio command errors
        elseif pkt_type == packet_types_index["TX_RADIO_CMD_ERR"] or pkt_type == packet_types_index["RX_RADIO_CMD_ERR"] then
            tree:add_le(pf_radio_error, tvbuf:range( 8, 4))
            tree:add_le(pf_uint32_data, tvbuf:range(12, 4))
        -- tx data acks
        elseif pkt_type == packet_types_index["TX_SIGNAL_DATA_ACK"] then
            tree:add_le(pf_uint64_data, tvbuf:range( 8, 8))
            tree:add_le(pf_uint32_data, tvbuf:range(16, 4))
            tree:add_le(pf_uint32_data, tvbuf:range(20, 4))
        end
    end
end

local udp_port = DissectorTable.get("udp.port")
udp_port:add(vxsdr_port, vxsdr)
