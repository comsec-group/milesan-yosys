/*
 *  yosys -- Yosys Open SYnthesis Suite
 *
 *  Copyright (C) 2020  Alberto Gonzalez <boqwxp@airmail.cc> & Flavien Solt <flsolt@ethz.ch>
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
 */

#include "kernel/register.h"
#include "kernel/rtlil.h"
#include "kernel/utils.h"
#include "kernel/log.h"
#include "kernel/yosys.h"

#include <algorithm>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct ControlRegistersProbesWorker {
private:
	// Command line arguments.
	bool opt_verbose;

	std::string sanitize_wire_name(std::string wire_name) {
		std::string ret;
		ret.reserve(wire_name.size());
		for(size_t char_id = 0; char_id < wire_name.size(); char_id++) {
			char curr_char = wire_name[char_id];
			if(curr_char != '$' && curr_char != ':' && curr_char != '.' && curr_char != '\\' && curr_char != '[' && curr_char != ']')
				ret += wire_name[char_id];
		}
		return '\\'+ret;
	}

	const RTLIL::IdString control_registers_probes_attribute_name = ID(regstate_cells_probes);

	void create_control_registers_probes(RTLIL::Module *module) {
		if (opt_verbose)
			log("Creating control registers probes for module %s.\n", module->name.c_str());

		if (module->processes.size())
			log_error("Unexpected process. Requires a `proc` pass before.\n");

		for(std::pair<RTLIL::IdString, RTLIL::Cell*> cell_pair : module->cells_) {
			RTLIL::IdString curr_cell_idstr = cell_pair.first;
			RTLIL::Cell *curr_cell = cell_pair.second;
			if (curr_cell->has_attribute(ID(regstate_cell))) {
				RTLIL::SigSpec port_q(curr_cell->getPort(ID::Q));
				// For each chunk in the output sigspec, create a new wire.
				int chunk_id = 0;
				for (auto &chunk_it: port_q.chunks()) {
					if (!chunk_it.is_wire())
						continue;

					std::string wire_name;
					wire_name = "crtlreg_prbsig"+std::to_string(curr_cell_idstr.index_)+"WIRE"+std::to_string(chunk_id)+"BITS"+std::to_string(chunk_it.offset)+"_"+std::to_string(chunk_it.offset+chunk_it.width)+"_";
					wire_name = sanitize_wire_name(wire_name);

					if (opt_verbose)
						log("Adding control register wire in module %s: %s (width: %i).\n", module->name.c_str(), wire_name.c_str(), chunk_it.width);

					Wire *new_wire = module->addWire(wire_name, chunk_it.width);
					module->connect(new_wire, chunk_it);

					new_wire->port_output = true;
					new_wire->set_bool_attribute(ID(regstate_cell_wire));

					module->fixup_ports();
					chunk_id++;
				}
			}
			else if (module->design->module(curr_cell->type) != nullptr) {

				RTLIL::Module *submodule = module->design->module(curr_cell->type);

				for (Wire *submodule_wire: submodule->wires()) {
					if (submodule_wire->has_attribute(ID(regstate_cell_wire))) {
						std::string wire_name = submodule_wire->name.str()+"INST"+curr_cell->name.str()+"PORT"+std::to_string(submodule_wire->port_id);
						wire_name = sanitize_wire_name(wire_name);
						if (opt_verbose)
							log("Adding wire in module %s from submodule %s (cell name %s) of type %s: %s\n", module->name.c_str(), submodule->name.c_str(), curr_cell->name.c_str(), curr_cell->type.c_str(), wire_name.c_str());
						Wire *new_wire = module->addWire(wire_name, submodule_wire->width);
						curr_cell->setPort(submodule_wire->name.str(), new_wire);

						new_wire->port_output = true;
						new_wire->set_bool_attribute(ID(regstate_cell_wire));
						module->fixup_ports();
					}
				}
			}
		}
		module->set_bool_attribute(control_registers_probes_attribute_name, true);
	}

public:
	ControlRegistersProbesWorker(RTLIL::Module *_module, bool _opt_verbose) {
		opt_verbose = _opt_verbose;

		create_control_registers_probes(_module);
	}
};

struct ControlRegistersProbesPass : public Pass {
	ControlRegistersProbesPass() : Pass("pull_control_registers_probes", "create taint probes reaching the selected module.") {}

	void help() override
	{
		//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
		log("\n");
		log("    pull_control_registers_probes\n");
		log("\n");
		log("Pulls up the control register probes.\n");
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

		log_header(design, "Executing pull_control_registers_probes pass (Concat port_control_registers_probes probe signals to form port).\n");

		if (GetSize(design->selected_modules()) == 0)
			log_cmd_error("Can't operate on an empty selection!\n");

		// Modules must be taken in inverted topological order to instrument the deepest modules first.
		// Taken from passes/techmap/flatten.cc
		TopoSort<RTLIL::Module*, IdString::compare_ptr_by_name<RTLIL::Module>> topo_modules;
		auto worklist = design->selected_modules();
		while (!worklist.empty()) {
			RTLIL::Module *module = *(worklist.begin());
			worklist.erase(worklist.begin());
			topo_modules.node(module);

			for (auto cell : module->selected_cells()) {
				RTLIL::Module *tpl = design->module(cell->type);
				if (tpl != nullptr) {
					if (topo_modules.get_database().count(tpl) == 0)
						worklist.push_back(tpl);
					topo_modules.edge(tpl, module);
				}
			}
		}
		if (!topo_modules.sort())
			log_cmd_error("Recursive modules are not supported by control_registers_probes.\n");

		// Run the worker on each module.
		for (auto i = 0; i < GetSize(topo_modules.sorted); ++i) {
			RTLIL::Module *module = topo_modules.sorted[i];
			ControlRegistersProbesWorker(module, opt_verbose);
		}
	}
} ControlRegistersProbesPass;

PRIVATE_NAMESPACE_END
