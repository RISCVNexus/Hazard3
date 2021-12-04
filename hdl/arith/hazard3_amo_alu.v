/**********************************************************************
 * DO WHAT THE FUCK YOU WANT TO AND DON'T BLAME US PUBLIC LICENSE     *
 *                    Version 3, April 2008                           *
 *                                                                    *
 * Copyright (C) 2021 Luke Wren                                       *
 *                                                                    *
 * Everyone is permitted to copy and distribute verbatim or modified  *
 * copies of this license document and accompanying software, and     *
 * changing either is allowed.                                        *
 *                                                                    *
 *   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION  *
 *                                                                    *
 * 0. You just DO WHAT THE FUCK YOU WANT TO.                          *
 * 1. We're NOT RESPONSIBLE WHEN IT DOESN'T FUCKING WORK.             *
 *                                                                    *
 *********************************************************************/

// Separate ALU for atomic memory operations

`default_nettype none
module hazard3_amo_alu #(
`include "hazard3_config.vh"
,
`include "hazard3_width_const.vh"
) (
	input  wire [W_MEMOP-1:0] op,
	input  wire [W_DATA-1:0]  op_rs1, // From load
	input  wire [W_DATA-1:0]  op_rs2, // From core
	output reg  [W_DATA-1:0]  result
);

wire sub          = op != MEMOP_AMOADD_W;
wire cmp_unsigned = op == MEMOP_AMOMINU_W || op == MEMOP_AMOMAXU_W;

wire [W_DATA-1:0] sum = op_rs1 + (op_rs2 ^ {W_DATA{sub}}) + sub;

wire rs1_lessthan_rs2 =
	op_rs1[W_DATA-1] == op_rs2[W_DATA-1] ? sum[W_DATA-1] :
	cmp_unsigned ?                      op_rs2[W_DATA-1] :
	                                    op_rs1[W_DATA-1] ;

always @ (*) begin
	case(op)
	MEMOP_AMOADD_W : result = sum;
	MEMOP_AMOXOR_W : result = op_rs1 ^ op_rs2;
	MEMOP_AMOAND_W : result = op_rs1 & op_rs2;
	MEMOP_AMOOR_W  : result = op_rs1 | op_rs2;
	MEMOP_AMOMIN_W : result = rs1_lessthan_rs2 ? op_rs1 : op_rs2;
	MEMOP_AMOMAX_W : result = rs1_lessthan_rs2 ? op_rs2 : op_rs1;
	MEMOP_AMOMINU_W: result = rs1_lessthan_rs2 ? op_rs1 : op_rs2;
	MEMOP_AMOMAXU_W: result = rs1_lessthan_rs2 ? op_rs2 : op_rs1;
	// AMOSWAP
	default:         result = op_rs2;
	endcase
end

endmodule

`default_nettype wire
