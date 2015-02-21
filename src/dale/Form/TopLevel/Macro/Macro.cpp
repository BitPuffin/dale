#include "Macro.h"
#include "Config.h"
#include "../../../Units/Units.h"
#include "../../../CoreForms/CoreForms.h"
#include "../../../Node/Node.h"
#include "../../Function/Function.h"
#include "../../Linkage/Linkage.h"
#include "../../ProcBody/ProcBody.h"
#include "../../Argument/Argument.h"

namespace dale
{
bool
FormTopLevelMacroParse(Units *units, Node *node)
{
    Context *ctx = units->top()->ctx;

    Node *top = node->list->at(2);
    const char *name = node->list->at(1)->token->str_value.c_str();

    /* Ensure this isn't core (core forms cannot be overridden
     * with a macro). */
    if (CoreForms::exists(name)) {
        Error *e = new Error(
            ErrorInst::Generator::NoCoreFormNameInMacro,
            top
        );
        ctx->er->addError(e);
        return false;
    }

    symlist *lst = top->list;

    if (lst->size() < 3) {
        Error *e = new Error(
            ErrorInst::Generator::IncorrectMinimumNumberOfArgs,
            top,
            "macro", 2, (int) (lst->size() - 1)
        );
        ctx->er->addError(e);
        return false;
    }

    int linkage = FormLinkageParse(ctx, (*lst)[1]);
    if (!linkage) {
        return false;
    }

    Type *r_type =
        ctx->tr->getPointerType(ctx->tr->getStructType("DNode"));

    /* Parse arguments - push onto the list that gets created. */

    Node *nargs = (*lst)[2];

    if (!nargs->is_list) {
        Error *e = new Error(
            ErrorInst::Generator::UnexpectedElement,
            nargs,
            "list", "macro parameters", "atom"
        );
        ctx->er->addError(e);
        return false;
    }

    symlist *args = nargs->list;

    Variable *var;

    std::vector<Variable *> *mc_args_internal =
        new std::vector<Variable *>;

    /* Parse argument - need to keep names. */

    std::vector<Node *>::iterator node_iter;
    node_iter = args->begin();

    bool varargs = false;

    /* An implicit MContext argument is added to every macro. */

    Type *pst = ctx->tr->getStructType("MContext");
    Type *ptt = ctx->tr->getPointerType(pst);

    Variable *var1 = new Variable(
        (char*)"mc", ptt
    );
    var1->linkage = Linkage::Auto;
    mc_args_internal->push_back(var1);

    int past_first = 0;

    while (node_iter != args->end()) {
        if (!(*node_iter)->is_token) {
            var = new Variable();
            FormArgumentParse(units, var, (*node_iter), false, false, false);
            if (!var->type) {
                return false;
            }
            mc_args_internal->push_back(var);
            ++node_iter;
        } else {
            if (!((*node_iter)->token->str_value.compare("void"))) {
                if (past_first || (args->size() > 1)) {
                    Error *e = new Error(
                        ErrorInst::Generator::VoidMustBeTheOnlyParameter,
                        nargs
                    );
                    ctx->er->addError(e);
                    return false;
                }
                break;
            }
            if (!((*node_iter)->token->str_value.compare("..."))) {
                if ((args->end() - node_iter) != 1) {
                    Error *e = new Error(
                        ErrorInst::Generator::VarArgsMustBeLastParameter,
                        nargs
                    );
                    ctx->er->addError(e);
                    return false;
                }
                var = new Variable();
                var->type = ctx->tr->type_varargs;
                var->linkage = Linkage::Auto;
                mc_args_internal->push_back(var);
                break;
            }
            var = new Variable();
            var->type = r_type;
            var->linkage = Linkage::Auto;
            var->name.append((*node_iter)->token->str_value);
            past_first = 1;
            mc_args_internal->push_back(var);
            ++node_iter;
        }
    }

    std::vector<llvm::Type*> mc_args;

    /* Convert to llvm args. The MContext argument is converted as per
     * its actual type. The remaining arguments, notwithstanding the
     * macro argument's 'actual' type, will always be (p DNode)s. */

    std::vector<Variable *>::iterator iter;
    iter = mc_args_internal->begin();
    llvm::Type *temp;

    int count = 0;
    while (iter != mc_args_internal->end()) {
        if ((*iter)->type->base_type == BaseType::VarArgs) {
            /* Varargs - finish. */
            varargs = true;
            break;
        }
        if (count == 0) {
            temp = ctx->toLLVMType((*iter)->type, NULL, false);
            if (!temp) {
                return false;
            }
        } else {
            temp = ctx->toLLVMType(r_type, NULL, false);
            if (!temp) {
                return false;
            }
        }
        mc_args.push_back(temp);
        ++count;
        ++iter;
    }

    temp = ctx->toLLVMType(r_type, NULL, false);
    if (!temp) {
        return false;
    }

    llvm::FunctionType *ft =
        getFunctionType(
            temp,
            mc_args,
            varargs
        );

    std::string new_name;

    ctx->ns()->functionNameToSymbol(name,
                            &new_name,
                            linkage,
                            mc_args_internal);

    if (units->top()->module->getFunction(llvm::StringRef(new_name.c_str()))) {
        Error *e = new Error(
            ErrorInst::Generator::RedeclarationOfFunctionOrMacro,
            top,
            name
        );
        ctx->er->addError(e);
        return false;
    }

    llvm::Constant *fnc =
        units->top()->module->getOrInsertFunction(
            new_name.c_str(),
            ft
        );

    llvm::Function *fn = llvm::dyn_cast<llvm::Function>(fnc);

    /* This is probably unnecessary, given the previous
     * getFunction call. */
    if ((!fn) || (fn->size())) {
        Error *e = new Error(
            ErrorInst::Generator::RedeclarationOfFunctionOrMacro,
            top,
            name
        );
        ctx->er->addError(e);
        return false;
    }

    fn->setCallingConv(llvm::CallingConv::C);

    fn->setLinkage(ctx->toLLVMLinkage(linkage));

    llvm::Function::arg_iterator largs = fn->arg_begin();

    /* Note that the values of the Variables of the macro's
     * parameter list will not necessarily match the Types of
     * those variables (to support overloading). */

    iter = mc_args_internal->begin();
    while (iter != mc_args_internal->end()) {
        if ((*iter)->type->base_type == BaseType::VarArgs) {
            break;
        }

        llvm::Value *temp = largs;
        ++largs;
        temp->setName((*iter)->name.c_str());
        (*iter)->value = temp;
        ++iter;
    }

    /* Add the macro to the context. */
    Function *dfn =
        new Function(r_type, mc_args_internal, fn, 1,
                              &new_name);
    dfn->linkage = linkage;

    if (!ctx->ns()->addFunction(name, dfn, top)) {
        return false;
    }
    if (units->top()->once_tag.length() > 0) {
        dfn->once_tag = units->top()->once_tag;
    }

    /* If the list has only three arguments, the macro is a
     * declaration and you can return straightaway. */

    if (lst->size() == 3) {
        return true;
    }

    /* Previously, Generator had a variable called
     * has_defined_extern_macro, which was set here if the macro had
     * extern scope.  Later, if that variable wasn't set,
     * createConstantMergePass could be added to the pass list.  If
     * problems are had later with that pass, the absence of that
     * variable and associated behaviour probably have something to do
     * with it.  */

    int error_count =
        ctx->er->getErrorTypeCount(ErrorType::Error);

    ctx->activateAnonymousNamespace();
    std::string anon_name = ctx->ns()->name;

    units->top()->pushGlobalFunction(dfn);
    FormProcBodyParse(units, top, dfn, fn, 3, 0);
    units->top()->popGlobalFunction();

    ctx->deactivateNamespace(anon_name.c_str());

    int error_post_count =
        ctx->er->getErrorTypeCount(ErrorType::Error);
    if (error_count != error_post_count) {
        std::map<std::string, std::vector<Function*>*
        >::iterator i = ctx->ns()->functions.find(name);
        if (i != ctx->ns()->functions.end()) {
            for (std::vector<Function *>::iterator
                    j = i->second->begin(),
                    k = i->second->end();
                    j != k;
                    ++j) {
                if ((*j)->is_macro) {
                    i->second->erase(j);
                    break;
                }
            }
        }
    }

    return true;
}
}
