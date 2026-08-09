local M = {}

--- Converts command arguments to a table
--- @param opts table
--- @return string|nil, table
M.convert_command_args = function(opts)
  local args = opts.fargs
  if #args == 0 then
    vim.notify("No arguments provided. Expected at least one argument.", vim.log.levels.ERROR)
    return nil, {}
  end

  local cmd = table.remove(args, 1)
  local cmd_opts = {}
  local positional_index = 1

  for _, arg in ipairs(args) do
    local key, value = arg:match("([^=]+)=(.*)")
    if key and value then
      -- Named argument (key=value)
      cmd_opts[key] = value
    else
      -- Positional argument
      cmd_opts[positional_index] = arg
      positional_index = positional_index + 1
    end
  end

  return cmd, cmd_opts
end

--- Converts a string to a boolean
--- @param value any
--- @return boolean?
local tobool = function(value)
  if not (value == "false" or value == "true") then
    return nil
  end
  return value == "true"
end

local all_types = {
  "nil",
  "boolean",
  "number",
  "string",
  "userdata",
  "function",
  "thread",
  "table"
}

M.convert_opts = function(default_opts, opts)
  for key, default_val in pairs(default_opts) do
    local expected_type
    if vim.tbl_contains(all_types, default_val) then
      expected_type = default_val
    else
      expected_type = type(default_val)
    end

    if opts[key] ~= nil then
      local new_val
      if expected_type == "boolean" then
        new_val = tobool(opts[key])
      elseif expected_type == "number" then
        new_val = tonumber(opts[key])
      elseif expected_type == "string" then
        new_val = opts[key]
      elseif expected_type == "table" then
        local ok, result = pcall(vim.fn.eval, opts[key])
        new_val = (ok and type(result) == "table") and result or nil
      end

      if new_val == nil then
        local msg = string.format("Invalid value for key '%s'. Expected type '%s'.", key, expected_type)
        vim.notify(msg, vim.log.levels.ERROR)
        return false
      end

      opts[key] = new_val
    end
  end
  return true
end

M.get_neogurt_channel = function()
  local uis = vim.api.nvim_list_uis()
  for _, ui in ipairs(uis) do
    local client = vim.api.nvim_get_chan_info(ui.chan).client
    if client and client.name == "neogurt" and client.type == "ui" then
      return ui.chan
    end
  end

  vim.notify("Cannot find neogurt client", vim.log.levels.ERROR)
  return nil
end

M.merge_opts = function(default_opts, opts)
  local new_opts = {}

  -- Handle positional arguments first
  local index = 1
  while default_opts[index] do
    local expected_type = default_opts[index]
    local val = opts[index]

    if val == nil then
      local msg = string.format("Positional argument #%d with type '%s' is required.", index, expected_type)
      vim.notify(msg, vim.log.levels.ERROR)
      return nil
    end

    if type(val) ~= expected_type then
      local msg = string.format("Invalid value for positional argument #%d. Expected type '%s'.", index, expected_type)
      vim.notify(msg, vim.log.levels.ERROR)
      return nil
    end

    new_opts[index] = val
    index = index + 1
  end

  for key, default_val in pairs(default_opts) do
    -- Skip positional arguments
    if type(key) == "number" then
      goto continue
    end

    local val = opts[key]
    local expected_type

    -- if default_val is a type, argument is required
    if vim.tbl_contains(all_types, default_val) then
      if val == nil then
        local msg = string.format("Key '%s' with type '%s' is required", key, default_val)
        vim.notify(msg, vim.log.levels.ERROR)
        return nil
      end
      expected_type = default_val
    else
      expected_type = type(default_val)
    end

    if val ~= nil then
      if type(opts[key]) ~= expected_type then
        local msg = string.format("Invalid value for key '%s'. Expected type '%s'.", key, expected_type)
        vim.notify(msg, vim.log.levels.ERROR)
        return nil
      end
      new_opts[key] = val
    else
      new_opts[key] = default_val
    end

    ::continue::
  end

  return new_opts
end

--- Converts numeric keys (positional arguments) in a table to "arg1", "arg2", ...
--- @param opts table
--- @return table
M.convert_positional_keys = function(opts)
  local converted_opts = {}

  for key, value in pairs(opts) do
    if type(key) == "number" then
      local new_key = "arg" .. tostring(key)
      converted_opts[new_key] = value
    else
      converted_opts[key] = value
    end
  end

  return converted_opts
end

return M
