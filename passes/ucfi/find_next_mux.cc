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

// For all new cells, add src=cell->get_src_attribute()

#include "kernel/log.h"
#include "kernel/register.h"
#include "kernel/rtlil.h"
#include "kernel/utils.h"
#include "kernel/yosys.h"

#include <deque>
#include <set>
#include <unordered_map>

USING_YOSYS_NAMESPACE

PRIVATE_NAMESPACE_BEGIN

// Create a type that is a pair of a module and a sigspec
typedef std::pair<RTLIL::Module *, RTLIL::SigSpec> module_sigspec_pair_t;

struct FindNextMuxWorker {
      private:
	// Command line arguments.
	bool opt_verbose = false;

	RTLIL::Module *start_module, *top_module = nullptr;
	const RTLIL::IdString find_next_mux_pass_name = ID(find_next_mux_pass);
	
	// Double-ended because connections without a cell in-between are added in front
	std::deque<module_sigspec_pair_t> next_to_explore_queue;
	// Write a set of already-explored pairs
	std::set<module_sigspec_pair_t> already_explored_set;

	std::unordered_map<RTLIL::Module *, RTLIL::Module *> module_to_parent_map;

	void initialize_module_to_parent_map(std::vector<RTLIL::Module *> all_modules) {
		for (auto module : all_modules) {
			for (auto cell : module->selected_cells()) {
				auto cell_type = cell->type;
				if (module->design->module(cell->type) != nullptr) {
					module_to_parent_map[module->design->module(cell->type)] = module;
				}
			}
		}
	}

	string find_better_wirename(RTLIL::Module *module, RTLIL::Wire *wire) {
		// Iterate through the module connections
		for (auto conn : module->connections()) {
			if (conn.second == wire) {
				if (conn.first.is_wire() && conn.first.as_wire()->name.str()[0] == '\\')
					return conn.first.as_wire()->name.str();
			}
			else if (conn.first == wire) {
				if (conn.second.is_wire() && conn.second.as_wire()->name.str()[0] == '\\')
					return conn.second.as_wire()->name.str();
			}
		}

		// Nothing better has been found
		return wire->name.str();
	}

	// Wire name, module name
	std::pair<string, string> find_next_mux(){
		// First, check whether the wire exists.
		while (next_to_explore_queue.size() > 0) {
			auto curr_pair = next_to_explore_queue.front();
			auto current_module = curr_pair.first;
			auto current_sigspec = curr_pair.second;
			next_to_explore_queue.pop_front();

			// Some assertions
			if (current_module == nullptr) {
				log_error("The current module is null.\n");
			}
			if (already_explored_set.count(curr_pair) > 0) {
				log("The current pair has already been explored: %s\n", current_sigspec.as_wire()->name.c_str());
				continue;
			}
			already_explored_set.insert(curr_pair);

			if (current_module->processes.size())
				log_error("Unexpected process. FindNextMuxPass requires a `proc` pass before.\n");

			for (auto curr_chunk : current_sigspec.chunks()) {
				if (!curr_chunk.wire) {
					log("The current chunk is not a wire.\n");
					continue;
				}

				log("Intermediate wire: %s (module: %s)\n", curr_chunk.wire->name.c_str(), current_module->name.c_str());

				// if (current_sigspec.is_wire()) {
				// 	log("Exploring wire %s in module %s\n", current_sigspec.as_wire()->name.c_str(), current_module->name.c_str());
				// } else {
				// 	log("Exploring sigspec in module %s\n", current_module->name.c_str());
				// }

				// Check whether the wire is the input to a mux.
				for (auto cell : current_module->selected_cells()) {
					for (auto port : cell->connections()) {
						// Check whether the cell is a mux.
						if (cell->type == ID($mux)) {
							if (port.second.is_wire() && port.second.as_wire()->name == curr_chunk.wire->name && cell->input(port.first)) {
								log("    Found mux with good port.\n");

								// Check whether this is the S port or another input port.
								if (port.first == ID::S) {
									log("    port.first == ID::S.\n");
									// Add the output port to the queue.
									next_to_explore_queue.push_back({current_module, cell->getPort(ID::Y)});
								}
								// If the S port is a reset signal
								else if (cell->getPort(ID::S).as_wire()->name.str().find("rstz") != std::string::npos) {
									log("    port.first == ID::S is rstz\n");
									// Add the output port to the queue.
									next_to_explore_queue.push_back({current_module, cell->getPort(ID::Y)});
								}
								else {
									log("The S port is not a reset signal. Good candidate.\n");
									// Return the S port
									RTLIL::SigSpec s_port = cell->getPort(ID::S);

									if (s_port.is_wire()) {
										string ret_modulename = current_module->name.str();
										string ret_wirename = find_better_wirename(current_module, s_port.as_wire());
										return {ret_wirename, ret_modulename};
									}
									else {
										string ret_modulename = current_module->name.str();
										string ret_wirename = find_better_wirename(current_module, s_port.as_chunk().wire);
										return {ret_wirename, ret_modulename};
									}
								}
							}
						}
					}
				}

				// If the wire is not the input to a mux, we need to explore the next wires.
				for (auto cell : current_module->selected_cells()) {
					// Ensure that this is not a module
					if (current_module->design->module(cell->type) != nullptr) {
						continue;
					}
					for (auto port : cell->connections()) {
						if (port.second.is_wire() && port.second.as_wire()->name == curr_chunk.wire->name) {
							// If the port is an input port, the we need to explore the cell's output ports.
							if (cell->input(port.first)) {
								for (auto output_port : cell->connections()) {
									if (cell->output(output_port.first)) {
										next_to_explore_queue.push_back({current_module, output_port.second});
										log("  Adding wire %s (module: %s) through cell type %s\n", output_port.second.as_wire()->name.c_str(), current_module->name.c_str(), cell->type.c_str());
									}
								}
							}
						}
					}
				}

				// Check for traditional connection sigsig
				for (auto conn : current_module->connections()) {
					if (conn.first.is_wire())
						log("  Traditional wire (first)  connection is: conn.first: %s\n", conn.first.as_wire()->name.c_str());
					if (conn.second.is_wire())
						log("  Traditional wire (second) connection is: conn.second: %s\n", conn.second.as_wire()->name.c_str());

					if (conn.second.is_wire() && conn.second.as_wire()->name == curr_chunk.wire->name) {
						next_to_explore_queue.push_front({current_module, conn.first});
						log("  Adding wire %s (module: %s) through traditional connection\n", conn.first.as_wire()->name.c_str(), current_module->name.c_str());
					}
				}

				// Check whether the wire is the input of a sub-module
				for (auto cell : current_module->selected_cells()) {
					auto cell_type = cell->type;
					if (current_module->design->module(cell->type) != nullptr) {
						for (auto port : cell->connections()) {
							if (port.second.is_wire() && port.second.as_wire()->name == curr_chunk.wire->name) {
								// Check that this is not an output port
								if (cell->output(port.first)) {
									continue;
								}

								next_to_explore_queue.push_front({current_module->design->module(cell->type), current_module->design->module(cell->type)->wire(port.first.str())});
								log("  Adding wire %s (module: %s) through submodule connection\n", cell->getPort(port.first).as_wire()->name.c_str(), current_module->design->module(cell->type)->name.c_str());
							}
							else
							{
								if (port.second.is_wire()) {
									log("  Port %s (idstr %s) is a wire but is not %s in submodule %s\n", port.second.as_wire()->name.c_str(), port.first.c_str(), curr_chunk.wire->name.c_str(), current_module->design->module(cell->type)->name.c_str());
								} else {
									log("  Port (idstr %s) is not a wire in module %s\n", port.first.c_str(), current_module->design->module(cell->type)->name.c_str());
								}
							}
						}
					}
				}

				// If the wire is an output of the module, then we go up by one module
				for (auto wire : current_module->wires()) {

					if (wire->name == curr_chunk.wire->name) {
						if (module_to_parent_map.count(current_module) > 0) {

							auto parent_module = module_to_parent_map[current_module];
							// Find the port that connects to the current wire
							for (auto cell : parent_module->selected_cells()) {
								if (parent_module->design->module(cell->type) == current_module) {
									for (auto port : cell->connections()) {
										if (port.first == curr_chunk.wire->name) {
											next_to_explore_queue.push_front({parent_module, cell->getPort(port.first)});
											log("  Adding wire %s (module: %s) through parent module connection\n", cell->getPort(port.first).as_wire()->name.c_str(), parent_module->name.c_str());
										}
									}
								}
							}
							// next_to_explore_queue.push({module_to_parent_map[current_module], module_to_parent_map[current_module]->wire(wire->name)});
						}
					}
				}
			}
		}
		return {"NONE", "NONE"};
	}

      public:
	FindNextMuxWorker(RTLIL::Module *_module, RTLIL::Module *_top_module, std::vector<RTLIL::Module *> all_modules, string start_wire_name, bool _opt_verbose)
	{
		start_module = _module;
		top_module = _top_module;

		initialize_module_to_parent_map(all_modules);

		opt_verbose = _opt_verbose;

		// Add the start wire to the queue.

		RTLIL::Wire *start_wire = start_module->wire("\\" + start_wire_name);
		if (start_wire == nullptr)
			log_error("The wire %s does not exist in module %s.\n", start_wire_name.c_str(), start_module->name.c_str());
		next_to_explore_queue.push_back({start_module, start_wire});

		auto port_and_module_names = find_next_mux();
		string port_name = port_and_module_names.first;
		string module_name = port_and_module_names.second;
		log("Mux select: %s\n", port_name.c_str());
		log("Module: %s\n", module_name.c_str());
	}
};

struct FindNextMussPass : public Pass {
	FindNextMussPass() : Pass("find_next_mux", "Find next multiplexer given the signal name.") {}

	void help() override
	{
		//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
		log("\n");
		log("    find_next_mux <name of the wire>\n");
		log("\n");
		log("Finds the next multiplexer.\n");
		log("\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		bool opt_verbose = false;
		string module_name = "";

		string opt_excluded_signals_csv;
		std::vector<string> opt_excluded_signals;

		log_header(design, "Looking for the next mux.\n");
		std::vector<std::string>::size_type argidx;

		if (args.size() < 2) {
			log_error("FindNextMussPass requires an argument: the name of the wire.");
		}

		string start_wire_name = args[1];

		for (argidx = 2; argidx < args.size(); argidx++) {
			if (args[argidx] == "-module") {
				module_name = args[++argidx];
				continue;
			}
			break;
		}
		extra_args(args, argidx, design);
		log("A D\n");

		// Check whether some module is selected.
		if (GetSize(design->selected_modules()) == 0)
			log_cmd_error("FindNextMussPass cannot operate on an empty selection.\n");
		log("A E\n");

		// Modules must be taken in inverted topological order to instrument the deepest modules first.
		// Taken from passes/techmap/flatten.cc
		TopoSort<RTLIL::Module *, IdString::compare_ptr_by_name<RTLIL::Module>> topo_modules;
		auto worklist = design->selected_modules();
		pool<RTLIL::IdString> non_top_modules;
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
					non_top_modules.insert(cell->type);
				}
			}
		}
		if (!topo_modules.sort())
			log_cmd_error("Recursive modules are not supported by FindNextMussPass.\n");

		// Find in which module the wire is.
		RTLIL::Module *module_with_the_wire = nullptr;
		bool module_with_name_found = module_name == "";
		string module_name_with_prefix = "\\" + module_name;

		log("Module name: %s\n", module_name.c_str());

		for (auto i = 0; i < GetSize(topo_modules.sorted); ++i) {
			RTLIL::Module *curr_module = topo_modules.sorted[i];

			// Check whether the name of curr_module contains module_name
			if (module_name != "" && std::string(curr_module->name.c_str()).find(module_name) == std::string::npos)
				continue;
			else
				module_with_name_found = true;

			// Print all the wires of the module
			for (auto wire : curr_module->wires()) {
				log("Wire: %s\n", wire->name.c_str());
			}

			if (curr_module->wire("\\" + start_wire_name) != nullptr) {
				if (module_with_the_wire != nullptr)
					log_error("The wire %s is present in more than one module.\n", start_wire_name.c_str());
				module_with_the_wire = curr_module;
			}
		}

		if (!module_with_name_found)
			log_error("The module %s does not exist.\n", module_name.c_str());
		if (module_with_the_wire == nullptr)
			log_error("The wire %s does not exist in any of the selected modules.\n", start_wire_name.c_str());

		FindNextMuxWorker(module_with_the_wire, design->top_module(), design->selected_modules(), start_wire_name, opt_verbose);		
	}
} FindNextMussPass;

PRIVATE_NAMESPACE_END
