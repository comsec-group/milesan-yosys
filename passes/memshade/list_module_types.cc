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

struct ListModulesPass : public Pass {
	std::set<std::string> module_type_names;

	ListModulesPass() : Pass("list_module_types", "List the module types.") {}

	void help() override
	{
		//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
		log("\n");
		log("    list_module_types\n");
		log("\n");
		log("Lists the module types.\n");
		log("\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		log_header(design, "Listing module types.\n");
		std::vector<std::string>::size_type argidx = 1;

		// if (args.size() < 2) {
		// 	log_error("ListModulesPass requires an argument: the name of the wire.");
		// }

		// for (argidx = 2; argidx < args.size(); argidx++) {
		// 	// if (args[argidx] == "-module") {
		// 	// 	module_name = args[++argidx];
		// 	// 	continue;
		// 	// }
		// 	break;
		// }
		extra_args(args, argidx, design);

		// Check whether some module is selected.
		if (GetSize(design->selected_modules()) == 0)
			log_cmd_error("Cannot operate on an empty selection.\n");

		// // Modules must be taken in inverted topological order to instrument the deepest modules first.
		// // Taken from passes/techmap/flatten.cc
		// TopoSort<RTLIL::Module *, IdString::compare_ptr_by_name<RTLIL::Module>> topo_modules;
		// auto worklist = design->selected_modules();
		// pool<RTLIL::IdString> non_top_modules;
		// while (!worklist.empty()) {
		// 	RTLIL::Module *module = *(worklist.begin());
		// 	worklist.erase(worklist.begin());
		// 	topo_modules.node(module);

		// 	for (auto cell : module->selected_cells()) {
		// 		RTLIL::Module *tpl = design->module(cell->type);
		// 		if (tpl != nullptr) {
		// 			if (topo_modules.get_database().count(tpl) == 0)
		// 				worklist.push_back(tpl);
		// 			topo_modules.edge(tpl, module);
		// 			non_top_modules.insert(cell->type);
		// 		}
		// 	}
		// }
		// if (!topo_modules.sort())
		// 	log_cmd_error("Recursive modules are not supported by ListModulesPass.\n");

		// for (auto i = 0; i < GetSize(topo_modules.sorted); ++i) {
		// 	// RTLIL::Module *module = topo_modules.sorted[i];
		// 	ListModulesWorker(topo_modules.sorted[i]);
		// }

		// Collect the module types
		for (auto curr_module : design->selected_modules()) {
			module_type_names.insert(curr_module->name.str());
		}

		// Sort them in alphabetical order
		std::vector<std::string> module_type_names_vec(module_type_names.begin(), module_type_names.end());
		std::sort(module_type_names_vec.begin(), module_type_names_vec.end());

		// Print them in alphabetical order
		for (auto module_type_name : module_type_names_vec) {
			// Remove the first character, which is a backslash
			module_type_name = module_type_name.substr(1);
			log("Module type: %s\n", module_type_name.c_str());
		}

		// design->selected_modules();
	}
} ListModulesPass;

PRIVATE_NAMESPACE_END
