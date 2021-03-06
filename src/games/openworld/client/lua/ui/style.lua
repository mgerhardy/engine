local module = {}

module.rowHeight = 50
module.colors = {
	['text'] = '#aaaaaa',
	['window'] = '#2d2d2d',
	['header'] = '#282828',
	['border'] = '#809090',
	['button'] = '#889933',
	['button hover'] = '#e6e666',
	['button active'] = '#e6e666',
	['toggle'] = '#646464',
	['toggle hover'] = '#787878',
	['toggle cursor'] = '#2d2d2d',
	['select'] = '#2d2d2d',
	['select active'] = '#809090',
	['slider'] = '#262626',
	['slider cursor'] = '#646464',
	['slider cursor hover'] = '#787878',
	['slider cursor active'] = '#969696',
	['property'] = '#262626',
	['edit'] = '#262626',
	['edit cursor'] = '#afafaf',
	['combo'] = '#2d2d2d',
	['chart'] = '#787878',
	['chart color'] = '#2d2d2d',
	['chart color highlight'] = '#ff0000',
	['scrollbar'] = '#282828',
	['scrollbar cursor'] = '#646464',
	['scrollbar cursor hover'] = '#787878',
	['scrollbar cursor active'] = '#969696',
	['tab header'] = '#282828'
}
module.style = {
	font = 20,
	button = {
		["normal"] = "#668080",
		["active"] = "#99cccc",
		["hover"] = "#99cccc",
		["border color"] = "#809090",
		["text normal"] = "#000000",
		["text hover"] = "#000000",
		["text active"] = "#000000"
	},
	checkbox = {
		["normal"] = "#889933",
		["active"] = "#2d2d2d",
		["hover"] = "#2d2d2d",
		["text background"] = "#ff0000",
		["cursor normal"] = "#2d2d2d",
	},
	window = {
		["header"] = {
			--[[
			["normal"] = "#646464",
			["active"] = "#889933",
			["hover"] = "#889933",
			--]]
			["label normal"] = "#668080",
			["label active"] = "#668080",
			["label hover"] = "#668080"
		}
	}
}

--[[
]]
function module.init()
	ui.styleLoadColors(module.colors)
	module.push(module.style)
end

--[[
]]
function module.shutdown()
	-- pop the style of the stack
	module.pop()
end

--[[
]]
function module.setRowHeight(rowHeight)
	module.rowHeight = rowHeight;
end

--[[
]]
function module.setFontSize(size)
	ui.styleSetFont(module.fontSize)
end

--[[
]]
function module.pushWindowBackground(colorOrImage)
	local style = {
		window = {["fixed background"] = colorOrImage}
	}
	module.push(style)
end

--[[
]]
function module.push(style)
	ui.stylePush(style)
end

--[[
]]
function module.pop()
	ui.stylePop()
end

return module
