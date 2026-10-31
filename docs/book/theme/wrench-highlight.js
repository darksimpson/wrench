// Syntax highlighting for ```wrench code blocks. mdBook bundles
// highlight.js 10 without a wrench grammar and highlights the page before
// this file runs, so the wrench blocks are highlighted again here. The
// keywords follow tools/vscode-wrench/syntaxes/wrench.tmLanguage.json.
(function ()
{
	if ( typeof hljs === "undefined" )
	{
		return;
	}

	hljs.registerLanguage( "wrench", function ( hljs )
	{
		return {
			name: "wrench",
			aliases: [ "w" ],
			keywords: {
				keyword: "if else while for do switch case default break continue return goto yield " +
					"var blank function struct enum export unit new include private",
				literal: "true false null",
				built_in: "print println"
			},
			contains: [
				hljs.C_LINE_COMMENT_MODE,
				hljs.C_BLOCK_COMMENT_MODE,
				hljs.QUOTE_STRING_MODE,
				{
					className: "string",
					begin: "'",
					end: "'",
					illegal: "\\n",
					contains: [ hljs.BACKSLASH_ESCAPE ]
				},
				{
					className: "number",
					variants: [
						{ begin: "\\b0[xX][0-9a-fA-F]+" },
						{ begin: "\\b0[bB][01]+" },
						{ begin: "(\\b\\d+(\\.\\d*)?|\\.\\d+)([eE][-+]?\\d+)?[fF]?" }
					],
					relevance: 0
				},
				{
					// library calls: math::sin, str::sprintf, sys::importCompile
					className: "built_in",
					begin: "\\b[A-Za-z_][A-Za-z0-9_]*::[A-Za-z_][A-Za-z0-9_]*",
					relevance: 0
				},
				{
					className: "title",
					begin: "(?<=\\b(?:function|struct|enum|unit)\\s+)[A-Za-z_][A-Za-z0-9_]*",
					relevance: 0
				}
			]
		};
	} );

	var blocks = document.querySelectorAll( "code.language-wrench" );
	for ( var i = 0; i < blocks.length; ++i )
	{
		var block = blocks[i];
		block.textContent = block.textContent; // drop the first pass's markup
		hljs.highlightBlock( block );
	}
})();
