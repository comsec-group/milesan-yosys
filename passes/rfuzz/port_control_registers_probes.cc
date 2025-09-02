/*
 *  yosys -- Yosys Open SYnthesis Suite
 *
 *  Copyright (C) 2022  Tobias Kovats <tkovats@student.ethz.ch> & Flavien Solt <flsolt@ethz.ch>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *  *
 *  This pass concatenates the cellift input and output wires to two separeate SigSpecs to form an input and output port. This
 *  is required by the harness generator.
 */

#include "kernel/yosys.h"
#include "kernel/log.h"
#include "kernel/rtlil.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

static std::map<string,int> count_wires(RTLIL::Module *module, RTLIL::IdString id){
	std::map<std::string,int> n_wires;
	n_wires["output"] = 0;
	n_wires["input"] = 0;
	for (auto wire_it : module->wires()){
		if(wire_it->has_attribute(id)){
			if(wire_it->port_output) n_wires["output"] += wire_it->width;
			else if(wire_it->port_input) n_wires["input"] += wire_it->width;
			else if(wire_it->port_input && wire_it->port_output) log_error("Wire %s is both input and output!", wire_it->name.c_str());
		}
	}
	return n_wires;
}

static void gen_control_registers_ports( RTLIL::Design *design, bool opt_verbose){
	RTLIL::Module *module = design->top_module();
	std::map<std::string,int> n_wires = count_wires(module, ID(regstate_cell_wire));

	// Locate the various control register wires on the port
	std::vector<int> port_start_indices, port_widths; 
	int next_index = 0;

	log("Creating ports for %i input and %i output control register wires.\n", n_wires["input"], n_wires["output"]);
	RTLIL::SigSpec control_register_input_wires = SigSpec();
	RTLIL::SigSpec control_register_output_wires = SigSpec();

    for (auto wire_it : module->wires()){

		if (!design->selected(module, wire_it))
			continue;

        if(wire_it->has_attribute(ID(regstate_cell_wire))){
            if(opt_verbose) log("Adding control register signal %s to port\n", RTLIL::id2cstr(wire_it->name));
			log_assert(wire_it->port_output);

			// Log the position and width
			port_start_indices.push_back(next_index);
			port_widths.push_back(wire_it->width);
			next_index += wire_it->width;

			control_register_output_wires.append(wire_it);
			wire_it->port_output = false; // wire is not needed as IO anymore, we have a copy in the output_port sigspec
			wire_it->set_bool_attribute(ID(regstate_cell_out)); // but mark it as input wire so we can get meta info in gen_toml
        }
    }

	RTLIL::Wire *control_register_output_port = module->addWire("\\auto_cover_out", control_register_output_wires.size());
	control_register_output_port->set_bool_attribute(ID(regstate_cell_out));
	module->connect(control_register_output_port, control_register_output_wires);
	control_register_output_port->port_output = true;
	control_register_output_port->set_bool_attribute(ID(regstate_cell_port));

	log("Start logging control register coordinates\n");
	for (size_t i = 0; i < port_start_indices.size(); i++)
		log("Control register %li starts at %i and has width %i\n", i, port_start_indices[i], port_widths[i]);
	log("End of  logging control register coordinates\n");

	module->fixup_ports();
}

struct PortMuxProbesPass : public Pass {
	PortMuxProbesPass() : Pass("port_control_registers_probes") { }

	void help() override
	{
		//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
		log("\n");
		log("    port_control_registers_probes\n");
		log("\n");
		log("Creates port for control register probes.\n");
		log("\n");
		log("Options:\n");
		log("\n");
		log("  -verbose\n");
		log("    Verbose mode.\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{

		bool opt_verbose = false;

		std::vector<std::string>::size_type argidx;
		for (argidx = 1; argidx < args.size(); argidx++) {
			if (args[argidx] == "-verbose") {
				opt_verbose = true;
				continue;
			}
		}

		log_header(design, "Executing port_control_registers_probes pass (Concat control register probe signals to form port).\n");
		gen_control_registers_ports(design, opt_verbose);

	}
} PortMuxProbesPass;

PRIVATE_NAMESPACE_END
