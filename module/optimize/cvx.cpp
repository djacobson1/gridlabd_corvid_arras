// module/optimize/cvx.cpp

#include "cvx.h"

EXPORT_CREATE(cvx);
EXPORT_INIT(cvx);
EXPORT_PRECOMMIT(cvx);
EXPORT_SYNC(cvx);
EXPORT_COMMIT(cvx);
EXPORT_FINALIZE(cvx);

EXPORT_METHOD(cvx,data)
EXPORT_METHOD(cvx,variables)
EXPORT_METHOD(cvx,objective)
EXPORT_METHOD(cvx,constraints)
EXPORT_METHOD(cvx,presolve)
EXPORT_METHOD(cvx,postsolve)

CLASS *cvx::oclass = NULL;
cvx *cvx::defaults = NULL;

// convenience objects
static PyObject *gld = NULL;
static PyObject *mod_list = NULL;
static PyObject *class_dict = NULL;
static PyObject *global_list = NULL;
static PyObject *object_dict = NULL;
static PyObject *property_type = NULL;

//
// Global variables
//

enumeration cvx::backend = cvx::BE_CPP;
enumeration cvx::failure_handling = cvx::OF_HALT;
PyObject *cvx::main_module = NULL;
PyObject *cvx::globals = NULL;
char1024 cvx::imports = "";
char1024 cvx::utils = "glm";
char1024 cvx::problemdump = "";

//
// Python Run function with formatting
//

int cvx::PyRun_FormatString(const char *format, ...)
{
    va_list ptr;
    va_start(ptr,format);
    char *buffer;
    vasprintf(&buffer,format,ptr);
    gl_verbose("Python call: %s",buffer);
    PyObject *result = PyRun_String(buffer,Py_file_input,globals,locals);
    int rc = 0;
    if ( result == NULL )
    {
        gl_verbose("Python call failed!");
        PyErr_Print();
        PyErr_Clear();
        rc = -1;
    }
    Py_XDECREF(result);
    free(buffer);
    va_end(ptr);
    return rc;
}

//
// CVX class creation
//

cvx::cvx(MODULE *module)
{
    if (oclass==NULL)
    {
        // register to receive notice for first top down. bottom up, and second top down synchronizations
        oclass = gld_class::create(module,"cvx",sizeof(cvx),PC_PRETOPDOWN|PC_BOTTOMUP|PC_POSTTOPDOWN|PC_AUTOLOCK|PC_OBSERVER);
        if (oclass==NULL)
            throw "unable to register class cvx";
        else
            oclass->trl = TRL_PROVEN;

        defaults = this;
        if (gl_publish_variable(oclass,

            PT_set, "event", get_event_offset(),
                PT_DEFAULT, "NONE",
                PT_KEYWORD, "INIT", (set)OE_INIT,
                PT_KEYWORD, "PRECOMMIT", (set)OE_PRECOMMIT,
                PT_KEYWORD, "PRESYNC", (set)OE_PRESYNC,
                PT_KEYWORD, "SYNC", (set)OE_SYNC,
                PT_KEYWORD, "POSTSYNC", (set)OE_POSTSYNC,
                PT_KEYWORD, "COMMIT", (set)OE_COMMIT,
                PT_KEYWORD, "FINALIZE", (set)OE_FINALIZE,
                PT_KEYWORD, "NONE", (set)OE_NONE,
                PT_DESCRIPTION, "event during which the optimization problem should be solved",

            PT_method, "presolve", get_presolve_offset(),
                PT_DESCRIPTION, "presolve script",

            PT_method, "postsolve", get_postsolve_offset(),
                PT_DESCRIPTION, "postsolve script",

            PT_char1024, "on_failure", get_on_failure_offset(),
                PT_DESCRIPTION, "on_failure script",

            PT_char1024, "on_exception", get_on_exception_offset(),
                PT_DESCRIPTION, "on_exception script",

            PT_char1024, "on_infeasible", get_on_infeasible_offset(),
                PT_DESCRIPTION, "on_infeasible script",

            PT_char1024, "on_unbounded", get_on_unbounded_offset(),
                PT_DESCRIPTION, "on_unbounded script",

            PT_method, "data", get_data_offset(),
                PT_DESCRIPTION, "problem data definition",

            PT_method, "variables", get_variables_offset(),
                PT_DESCRIPTION, "problem variable definitions",
                
            PT_method, "objective", get_objective_offset(),
                PT_REQUIRED,
                PT_DESCRIPTION, "problem objective expression",
                
            PT_method, "constraints", get_constraints_offset(),
                PT_DESCRIPTION, "problem constraint expressions",
            
            PT_enumeration, "status", get_status_offset(),
                PT_KEYWORD, "INIT", (enumeration)SS_INIT,
                PT_KEYWORD, "READY", (enumeration)SS_READY,
                PT_KEYWORD, "OPTIMAL", (enumeration)SS_OPTIMAL,
                PT_KEYWORD, "INACCURATE", (enumeration)SS_INACCURATE,
                PT_KEYWORD, "INFEASIBLE", (enumeration)SS_INFEASIBLE,
                PT_KEYWORD, "UNBOUNDED", (enumeration)SS_UNBOUNDED,
                PT_KEYWORD, "INVALID", (enumeration)SS_INVALID,
                PT_KEYWORD, "ERROR", (enumeration)SS_ERROR,
                PT_DESCRIPTION, "solver status",

            PT_double, "value", get_value_offset(),
                PT_OUTPUT,
                PT_DESCRIPTION, "optimal value",    

            PT_char1024, "solver_options", get_solver_options_offset(),
                PT_DESCRIPTION, "solver options to add to problem.solve() arguments",

            PT_double, "update_interval[s]", get_update_interval_offset(),
                PT_DESCRIPTION, "minimum optimization update interval in seconds",

            NULL)<1)
        {
            exception("unable to publish optimize/cvx properties");
        }

        gl_global_create("optimize::cvx_backend",PT_enumeration,&backend,
            PT_KEYWORD, "CPP", (enumeration)BE_CPP,
            PT_KEYWORD, "SCIPY", (enumeration)BE_SCIPY,
            PT_KEYWORD, "NUMPY", (enumeration)BE_NUMPY,
            PT_DESCRIPTION,"CVX backend implementation",NULL);

        gl_global_create("optimize::cvx_failure_handling",PT_enumeration,&failure_handling,
            PT_KEYWORD, "HALT", (enumeration)OF_HALT,
            PT_KEYWORD, "WARN", (enumeration)OF_WARN,
            PT_KEYWORD, "IGNORE", (enumeration)OF_IGNORE,
            PT_KEYWORD, "RETRY", (enumeration)OF_RETRY,
            PT_DESCRIPTION, "CVX failure handling", NULL);

        gl_global_create("optimize::cvx_imports",PT_char1024,&imports,
            PT_DESCRIPTION, "CVX symbols to import", NULL);

        gl_global_create("optimize::network",PT_char1024,&utils,
            PT_DESCRIPTION, "global symbol to use as network.py import", NULL);

        gl_global_create("optimize::cvx_problemdump",PT_char1024,&problemdump,
            PT_DESCRIPTION, "CVX problem dump filename", NULL);

        main_module = PyImport_AddModule("__main__");
        if ( main_module == NULL )
        {
            exception("unable to access python __main__ module");
        }

        // setup globals
        globals = PyDict_New(); // PyModule_GetDict(main_module);
        if ( globals == NULL )
        {
            exception("unable to access python globals in __main__");
        }

        // create __cvx__ global
        PyDict_SetItemString(globals,"__cvx__",PyDict_New());

        // create gld global
        gld = PyModule_New("gld");
        mod_list = PyList_New(0);
        class_dict = PyDict_New();
        global_list = PyList_New(0);
        object_dict = PyDict_New();
        PyModule_AddObject(gld,"modules",mod_list);
        PyModule_AddObject(gld,"classes",class_dict);
        PyModule_AddObject(gld,"globals",global_list);
        PyModule_AddObject(gld,"objects",object_dict);
        PyDict_SetItemString(globals,"gld",gld);
    }
}

//
// CVX Object creation
//

int cvx::create(void) 
{
    // initialize properties
    set_event(OE_NONE);
    presolve_py = strdup("");
    postsolve_py = strdup("");
    set_on_failure("");
    set_on_exception("");
    set_on_infeasible("");
    set_on_unbounded("");
    set_value(QNAN);
    set_status(SS_INIT);

    // setup problem data
    problem.objective = strdup("");
    problem.enable_dpp = false;
    problem.data = NULL;
    problem.variables = NULL;
    problem.constraints = NULL;

    // load python modules if needed
    if ( PyDict_GetItemString(globals,"cvx") == NULL )
    {
        if ( PyRun_FormatString("import os, sys") != 0 )
        {
            exception("unable to import os, sys");
        }

        // load cvxpy module
        cvxpy = PyImport_ImportModule("cvxpy");
        if ( cvxpy == NULL )
        {
            if ( PyErr_Occurred() )
            {
                PyErr_Print();
            }
            exception("unable to load cvxpy module");
        }

        // import cvxpy module
        if ( PyRun_FormatString("import cvxpy as cvx") != 0 )
        {
            exception("unable to import cvxpy");
        }

        // import requested definitions from cvxpy, if any
        if ( PyRun_FormatString("from cvxpy import %s", strcmp(imports,"") != 0 ? (const char*)imports : "*") != 0 )
        {
            exception("unable to import cvxpy symbols");
        }

        // import numpy
        if ( PyRun_FormatString("import numpy as np") != 0 )
        {
            exception("unable to load numpy");
        }
    }

    // setup locals
    locals = PyDict_New();
    current_event = "NONE";

    return 1; /* return 1 on success, 0 on failure */
}

//
// CVX Object initialization
//

int cvx::init(OBJECT *parent)
{
    current_event = "INIT";

    // add property accessor if not already done
    if ( property_type == NULL )
    {
        property_type = callback->python.property_type();
        if ( PyModule_AddObject(gld,"property",property_type) < 0 )
        {
            exception("unable to add property accessor to gld module");
        }

        // construct object list
        for ( OBJECT *obj = gl_object_get_first() ; obj != NULL ; obj = obj->next )
        {
            gld_object *data = get_object(obj);
            PyObject *values = PyDict_New();
            PyDict_SetItemString(values,"class",PyUnicode_FromString(data->get_oclass()->get_name()));
            PyDict_SetItemString(values,"id",PyLong_FromLong(data->get_id()));
            if ( isfinite(data->get_latitude()) )
            {
                PyDict_SetItemString(values,"latitude",PyFloat_FromDouble(data->get_latitude()));
            }
            if ( isfinite(data->get_longitude()) )
            {
                PyDict_SetItemString(values,"longitude",PyFloat_FromDouble(data->get_longitude()));
            }
            if ( strcmp(data->get_groupid(),"") != 0 )
            {
                PyDict_SetItemString(values,"group",PyUnicode_FromString(data->get_groupid()));
            }
            if ( data->get_parent() != NULL )
            {
                PyDict_SetItemString(values,"parent",PyUnicode_FromString(data->get_parent()->get_name()));
            }
            PyDict_SetItemString(object_dict,data->get_name(),values);
        }

        // construct global list
        for ( GLOBALVAR *var = gl_global_get_next(NULL) ; var != NULL ; var = var->next )
        {
            PyList_Append(global_list,PyUnicode_FromString(var->prop->name));
        }

        // construct module list
        for ( MODULE *module = gl_module_getfirst() ; module != NULL ; module = module->next )
        {
            PyList_Append(mod_list,PyUnicode_FromString(module->name));
        }

        // construct class data dictionary
        for ( CLASS *oclass = gl_class_get_first() ; oclass != NULL ; oclass = oclass->next )
        {
            PyObject *defs = PyList_New(0);
            for ( PROPERTY *prop = oclass->pmap ; prop != NULL && prop->oclass == oclass ; prop = prop->next )
            {
                PyList_Append(defs,PyUnicode_FromString(prop->name));
            }
            PyDict_SetItemString(class_dict,oclass->name,defs);
        }

        PyObject *module = PyImport_ImportModule("gridlabd.network");
        if ( module == NULL )
        {
            exception("'import gridlabd.network as %s' failed (PYTHONPATH=%s)",(const char*)utils,getenv("PYTHONPATH"));
        }
        PyModule_AddObject(gld,"glm",module);
        PyModule_AddObject(module,"gld",gld);
        PyDict_SetItemString(globals,utils,module);
    }

    // setup problem dump
    if ( PyDict_GetItemString(globals,"__dump__") == NULL )
    {
        gld_clock now;
        if ( strcmp(problemdump,"") == 0 )
        {
            PyRun_FormatString("global __dump__; __dump__ = open(os.devnull,'w')");
        }
        else if ( PyRun_FormatString("global __dump__; __dump__ = open('%s','w'); print('Problem dumps starting at t=%lld (%s)',file=__dump__,flush=True);",(const char*)problemdump,gl_globalclock,now.get_string().get_buffer()) < 0 )
        {
            exception("unable to open dumpfile '%s'",(const char*)problemdump);
        }
    }

    // objective must be specified
    if ( strlen(problem.objective) == 0 )
    {
        exception("missing problem objective");
    }

    // create this optimizers data in __cvx__ global
    PyDict_SetItemString(PyDict_GetItemString(globals,"__cvx__"),(const char*)get_name(),PyDict_New());

    // initialization solution event handler
    if ( get_event(OE_INIT) && ! update_solution() )
    {
        // warn on failure if required
        if ( failure_handling == OF_WARN )
        {
            warning("optimization failed during init event");
        }

        // handle failure
        return failure_handling != OF_HALT ? 1 : 0;
    }

    if ( get_event() == OE_NONE )
    {
        warning("problem has no event handler specified and will never be solved");
    }

    update_status();

    return 1; // should use deferred optimization until all input objects are initialized
}

// 
// Event handlers
//

int cvx::precommit(TIMESTAMP t0)
{
    if ( fabs(update_interval) < 1 || t0%TIMESTAMP(update_interval) == 0 )
    {        
        current_event = "PRECOMMIT";

        if ( get_event(OE_PRECOMMIT) && ! update_solution() )
        {
            if ( failure_handling == OF_RETRY )
            {
                return t0;
            }
            if ( failure_handling == OF_WARN )
            {
                warning("optimization failed during precommit event");
            }
            return failure_handling != OF_HALT ? 1 : 0;
        }
    }
    return 1;
}

TIMESTAMP cvx::presync(TIMESTAMP t0)
{
    if ( fabs(update_interval) < 1 || t0%TIMESTAMP(update_interval) == 0 )
    {
        current_event = "PRESYNC";

        if ( get_event(OE_PRESYNC) && ! update_solution() )
        {
            if ( failure_handling == OF_RETRY )
            {
                return t0;
            }
            if ( failure_handling == OF_WARN )
            {
                warning("optimization failed during presync event");
            }
            return failure_handling != OF_HALT ? TS_NEVER : TS_INVALID;
        }
    }
    return fabs(update_interval) < 1 ? TS_NEVER : (TIMESTAMP)((TIMESTAMP(t0/update_interval)+1)*update_interval);
}

TIMESTAMP cvx::sync(TIMESTAMP t0)
{
    if ( fabs(update_interval) < 1 || t0%TIMESTAMP(update_interval) == 0 )
    {
        current_event = "SYNC";

        if ( get_event(OE_SYNC) && ! update_solution() )
        {
            if ( failure_handling == OF_RETRY )
            {
                return t0;
            }
            if ( failure_handling == OF_WARN )
            {
                warning("optimization failed during sync event");
            }
            return failure_handling != OF_HALT ? TS_NEVER : TS_INVALID;
        }
    }
    return fabs(update_interval) < 1 ? TS_NEVER : (TIMESTAMP)((TIMESTAMP(t0/update_interval)+1)*update_interval);
}

TIMESTAMP cvx::postsync(TIMESTAMP t0)
{
    if ( fabs(update_interval) < 1 || t0%TIMESTAMP(update_interval) == 0 )
    {
        current_event = "POSTSYNC";

        if ( get_event(OE_POSTSYNC) && ! update_solution() )
        {
            if ( failure_handling == OF_RETRY )
            {
                return t0;
            }
            if ( failure_handling == OF_WARN )
            {
                warning("optimization failed during postsync event");
            }
            return failure_handling != OF_HALT ? TS_NEVER : TS_INVALID;
        }
    }
    return fabs(update_interval) < 1 ? TS_NEVER : (TIMESTAMP)((TIMESTAMP(t0/update_interval)+1)*update_interval);
}

TIMESTAMP cvx::commit(TIMESTAMP t0,TIMESTAMP t1)
{
    if ( fabs(update_interval) < 1 || t0%TIMESTAMP(update_interval) == 0 )
    {
        current_event = "COMMIT";

        if ( get_event(OE_COMMIT) && ! update_solution() )
        {
            if ( failure_handling == OF_RETRY )
            {
                return t0;
            }
            if ( failure_handling == OF_WARN )
            {
                warning("optimization failed during commit event");
            }
            return failure_handling != OF_HALT ? TS_NEVER : TS_INVALID;
        }
    }
    return fabs(update_interval) < 1 ? TS_NEVER : (TIMESTAMP)((TIMESTAMP(t0/update_interval)+1)*update_interval);
}

int cvx::finalize(void)
{
    if ( fabs(update_interval) < 1 || gl_globalclock%TIMESTAMP(update_interval) == 0 )
    {
        current_event = "FINALIZE";

        if ( get_event(OE_FINALIZE) && ! update_solution() )
        {
            if ( failure_handling == OF_RETRY )
            {
                exception("unable to retry solution on finalize event");
            }
            if ( failure_handling == OF_WARN )
            {
                warning("optimization failed during finalize event");
            }
            return failure_handling != OF_HALT ? 1 : 0;
        }
    }
    return 1;
}

//
// Data request
// 

int cvx::data(
    char *buffer, // read/write buffer (NULL for size check/request)
    size_t len // write buffer len (0 for read request or size check)
    )
{
    if ( buffer == NULL ) // compute size of write request
    {
        int result = 0;
        for ( DATA *item = problem.data ; item != NULL ; item = item->next )
        {
            result += strlen(item->spec) + ( result > 0 ? 1 : 0 );
        }
        if ( len == 0 )
        {
            // return length of result only
            return result;
        }
        else
        {
            // return non-zero if len > length of result
            return (int)len > result ? result : 0;
        }
    }
    else if ( len == 0 ) // read buffer into object data
    {
        // return number of characters read from buffer
        return add_data(problem,buffer) ? strlen(buffer) : 0;
    }
    else // write object data into buffer
    {
        int result = 0;
        for ( DATA *item = problem.data ; item != NULL ; item = item->next )
        {
            result += snprintf(buffer+result,len-result,"%s%s",result>0?";":"",item->spec);
        }
        // return number of characters written to buffer
        return result;
    }
}

//
// Variable request
//

int cvx::variables(
    char *buffer, // read/write buffer (NULL for size request)
    size_t len // write buffer len (0 for read request or size check)
    )
{
    if ( buffer == NULL ) // compute size of write request
    {
        int result = 0;
        for ( VARIABLE *item = problem.variables ; item != NULL ; item = item->next )
        {
            result += strlen(item->spec) + ( result > 0 ? 1 : 0 );
        }
        if ( len == 0 )
        {
            // return length of result only
            return result;
        }
        else
        {
            // return non-zero if len > length of result
            return (int)len > result ? result : 0;
        }
    }
    else if ( len == 0 ) // read buffer into object data
    {
        // return number of characters read from buffer
        return add_variables(problem,buffer) ? strlen(buffer) : 0;
    }
    else // write object data into buffer
    {
        int result = 0;
        for ( VARIABLE *item = problem.variables ; item != NULL ; item = item->next )
        {
            result += snprintf(buffer+result,len-result,"%s%s",result>0?";":"",item->spec);
        }
        // return number of characters written to buffer
        return result;
    }
}

//
// Objective request
//

int cvx::objective(
    char *buffer, // read/write buffer (NULL for size request)
    size_t len // write buffer len (0 for read request or size check)
    )
{
    if ( buffer == NULL ) // compute size of write request
    {
        size_t result = strlen(problem.objective);
        if ( len == 0 )
        {
            // return length of result only
            return result;
        }
        else
        {
            // return non-zero if len > length of result
            return len>result ? result : 0;
        }
    }
    else if ( len == 0 ) // read buffer into object data
    {
        // return number of characters read from buffer
        return set_objective(problem,buffer) ? strlen(problem.objective) : 0;
    }
    else // write object data into buffer
    {
        // return number of characters written to buffer
        return snprintf(buffer,len,"%s",problem.objective);
    }
}

//
// Constraint request
//

int cvx::constraints(
    char *buffer, // read/write buffer (NULL for size request)
    size_t len // write buffer len (0 for read request or size check)
    )    
{
    if ( buffer == NULL ) // compute size of write request
    {
        int result = 0;
        for ( CONSTRAINT *item = problem.constraints ; item != NULL ; item = item->next )
        {
            result += strlen(item->spec) + ( result > 0 ? 1 : 0 );
        }
        if ( len == 0 )
        {
            // return length of result only
            return result;
        }
        else
        {
            // return non-zero if len > length of result
            return (int)len > result ? result : 0;
        }
    }
    else if ( len == 0 ) // read buffer into object data
    {
        // return number of characters read from buffer
        return add_constraints(problem,buffer) ? strlen(buffer) : 0;
    }
    else // write object data into buffer
    {
        int result = 0;
        for ( CONSTRAINT *item = problem.constraints ; item != NULL ; item = item->next )
        {
            result += snprintf(buffer+result,len-result,"%s%s",result>0?",":"",item->spec);
        }
        // return number of characters written to buffer
        return result;
    }
}

//
// Pre/Post solve scripts
//

static int unindent(char *text)
{
    bool eol = (text[0] == '\n');
    int indent = 0, last = 0;
    bool first = true;
    char *to = text;
    for ( char *from=(eol?text+1:text) ; *from != '\0' ; from++ )
    {
        if ( eol && *from == ' ' && first )
        {
            last = ++indent;
            continue;
        }
        first = false;
        if ( eol && last++ < indent )
        {
            continue;
        }
        last = 0;
        *to++ = *from;
        eol = ( *from == '\n' );
    }
    *to = '\0';
    return to-text;
}

int cvx::presolve(
    char *buffer, // read/write buffer (NULL for size request)
    size_t len // write buffer len (0 for read request or size check)
    )    
{
    if ( buffer == NULL ) // compute size of write request
    {
        int result = strlen(presolve_py) + 1;
        if ( len == 0 )
        {
            // return length of result only
            return result;
        }
        else
        {
            // return non-zero if len > length of result
            return (int)len > result ? result : 0;
        }
    }
    else if ( len == 0 ) // read buffer into object data
    {
        // return number of characters read from buffer
        free(presolve_py);
        presolve_py = strdup(buffer);
        return unindent(presolve_py);
    }
    else // write object data into buffer
    {
        return snprintf(buffer,len-1,"%s",presolve_py);
    }
}

int cvx::postsolve(
    char *buffer, // read/write buffer (NULL for size request)
    size_t len // write buffer len (0 for read request or size check)
    )    
{
    if ( buffer == NULL ) // compute size of write request
    {
        int result = strlen(postsolve_py) + 1;
        if ( len == 0 )
        {
            // return length of result only
            return result;
        }
        else
        {
            // return non-zero if len > length of result
            return (int)len > result ? result : 0;
        }
    }
    else if ( len == 0 ) // read buffer into object data
    {
        // return number of characters read from buffer
        free(postsolve_py);
        postsolve_py = strdup(buffer);
        return unindent(postsolve_py);
    }
    else // write object data into buffer
    {
        return snprintf(buffer,len-1,"%s",postsolve_py);
    }
}

//
// Link object properties to CVX data
//

bool cvx::add_data(struct s_problem &problem, const char *spec)
// Format of spec: "NAME:[OBJECT.|CLASS:.|GROUP@|]PROPERTY[,...][;...]"
{
    if ( strchr(spec,';') != NULL )
    {
        char *buffer = strdup(spec);
        if ( buffer == NULL )
        {
            exception("memory allocation error");
            return false;
        }
        char *item, *next = buffer;
        while ( (item=strsep(&next,";")) != NULL )
        {
            if ( ! add_data(problem,item) )
            {
                free(buffer);
                return false;
            }
        }
        free(buffer);
        return true;
    }
    else
    {
        DATA *item = (DATA*)malloc(sizeof(DATA));
        if ( item == NULL )
        {
            exception("memory allocation error");
        }
        item->spec = strdup(spec);
        if ( item->spec == NULL )
        {
            exception("memory allocation error");
        }
        char *buffer = strdup(spec);
        if ( buffer == NULL )
        {
            exception("memory allocation error");
        }
        char *speclist = buffer;
        item->name = strdup(strsep(&speclist,"="));
        if ( item->name == NULL )
        {
            exception("memory allocation error");
        }
        if ( speclist == NULL )
        {
            exception("variable specification '%s' missing expected '='", buffer);
        }
        if ( item->name[0] == '~' ) // prefix for parameters
        {
            item->name++;
            item->is_parameter = true;
            problem.enable_dpp = true;
        }
        item->data = NULL;
        item->list = PyList_New(0);
        item->columns = item->rows = 0;
        if ( item->list == NULL )
        {
            exception("memory allocation error");
        }

        // check for duplicate specification
        for ( DATA *var = problem.data ; var != NULL ; var = var->next )
        {
            if ( strcmp(var->name,item->name) == 0 )
            {
                exception("variable '%s' already specified, ignoring duplicate definition",item->name);
            }
        }   

        // link properties
        char *varspec;
        while (  (varspec=strsep(&speclist,",")) != NULL )
        {
            char classname[65], groupname[65], objectname[65], propname[65];
            if ( sscanf(varspec,"%[^:]:%[^&]",classname,propname) == 2 )
            {
                add_data_class(item,classname,propname);
            }
            else if ( sscanf(varspec,"%[^@]@%[^/]",groupname,propname) == 2 )
            {
                add_data_group(item,groupname,propname);
            }
            else if ( sscanf(varspec,"%[^.].%[^&]",objectname,propname) == 2 )
            {
                add_data_object(item,objectname,propname);
            }
            else if ( sscanf(varspec,"%[^&]",propname) == 1 )
            {
                add_data_global(item,propname);
            }
            else
            {
                exception("invalid data spec '%s'",varspec);
            }
        }

        // publish to globals
        PyDict_SetItemString(globals,item->name,item->list);

        // link to list
        item->next = problem.data;
        problem.data = item;
        free(buffer);
        return true;
    }
}

void cvx::add_data_class(DATA *item, const char *classname, const char *propname)
{
    CLASS *oclass = gl_class_get_by_name(classname,NULL);
    unsigned int count = 0;
    for ( gld_object *obj = gld_object::get_first() ; obj != NULL ; obj = obj->get_next() )
    {
        if ( (CLASS*)(obj->get_oclass()) == oclass )
        {
            gld_property data(obj,propname);
            if ( ! data.is_valid() )
            {
                exception("data item '%s' is not a known property of class '%s'",propname,classname);
            }
            if ( ! data.is_double() )
            {
                exception("data item '%s.%s' is not a real number",classname,propname);
            }
            
            REFERENCE *ref = new REFERENCE;
            if ( ref == NULL )
            {
                exception("memory allocation error");
            }
            ref->ptr = (double*)data.get_addr();
            ref->next = item->data;
            item->data = ref;
            PyList_Append(item->list,PyFloat_FromDouble(*(ref->ptr)));
            count++;
        }
    }
    if ( count == 0 )
    {
        warning("class '%s' has no objects",classname);
    }
    if ( item->rows > 0 )
    {
        if ( item->rows != count )
        {
            error("class '%s' rows do not match",classname);
        }
    }
    else
    {
        item->rows = count;
    }
    item->columns++;
}

void cvx::add_data_group(DATA *item, const char *groupname, const char *propname)
{
    unsigned int count = 0;
    for ( OBJECT *obj = gl_object_get_first() ; obj != NULL ; obj = obj->next )
    {
        if ( strcmp(obj->groupid,groupname) == 0 )
        {
            gld_property data(obj,propname);
            if ( ! data.is_valid() )
            {
                exception("data item '%s' is not a known property of object '%s' in group '%s'",propname,get_object(obj)->get_name(),groupname);
            }
            if ( ! data.is_double() )
            {
                exception("property '%s.%s' is not a real number",get_object(obj)->get_name(),propname);
            }
            
            REFERENCE *ref = new REFERENCE;
            if ( ref == NULL )
            {
                exception("memory allocation error");
            }
            ref->ptr = (double*)data.get_addr();
            ref->next = item->data;
            item->data = ref;
            PyList_Append(item->list,PyFloat_FromDouble(*(ref->ptr)));
            count++;
        }
    }
    if ( count == 0 )
    {
        warning("group '%s' has no objects",groupname);
    }
    if ( item->rows > 0 )
    {
        if ( item->rows != count )
        {
            error("group '%s' rows do not match",groupname);
        }
    }
    else
    {
        item->rows = count;
    }
    item->columns++;
}

void cvx::add_data_object(DATA *item, const char *objectname, const char *propname)
{
    gld_property data(objectname,propname);
    if ( ! data.is_valid() )
    {
        exception("data item '%s' is not a known property of object '%s'",propname,objectname);
    }
    if ( ! data.is_double() )
    {
        exception("property '%s.%s' is not a real number",objectname,propname);
    }
    
    REFERENCE *ref = new REFERENCE;
    if ( ref == NULL )
    {
        exception("memory allocation error");
    }
    ref->ptr = (double*)data.get_addr();
    ref->next = item->data;
    item->data = ref;
    PyList_Append(item->list,PyFloat_FromDouble(*(ref->ptr)));
    unsigned int count = 1;
    if ( item->rows > 0 )
    {
        if ( item->rows != count )
        {
            error("object '%s' rows do not match",objectname);
        }
    }
    else
    {
        item->rows = count;
    }
    item->columns++;
}

void cvx::add_data_global(DATA *item, const char *globalname)
{
    gld_global data(globalname);
    if ( ! data.is_valid() )
    {
        exception("data item '%s' is not a known global variable",globalname);
    }
    if ( data.get_property()->ptype != PT_double )
    {
        exception("global variable '%s' is not a real number",globalname);
    }
    REFERENCE *ref = new REFERENCE;
    if ( ref == NULL )
    {
        exception("memory allocation error");
    }
    ref->ptr = (double*)(data.get_property()->addr);
    ref->next = item->data;
    item->data = ref;
    PyList_Append(item->list,PyFloat_FromDouble(*(ref->ptr)));
    unsigned int count = 1;
    if ( item->rows > 0 )
    {
        if ( item->rows != count )
        {
            error("global '%s' rows do not match",globalname);
        }
    }
    else
    {
        item->rows = count;
    }
    item->columns++;
}

//
// Link object properties to CVX variable
//

bool cvx::add_variables(struct s_problem &problem, const char *spec)
// Format of spec: 
//   "NAME=CLASS:PROPERTY[&DUAL][;...]"
//   "NAME=GROUP@PROPERTY[&DUAL][;...]"
//   "NAME=OBJECT.PROPERTY[&DUAL][;...]"
//   "NAME=PROPERTY[&DUAL][;...]"
{
    if ( strchr(spec,';') != NULL )
    {
        char *buffer = strdup(spec);
        if ( buffer == NULL )
        {
            exception("memory allocation error");
            return false;
        }
        char *item, *next = buffer;
        while ( (item=strsep(&next,";")) != NULL )
        {
            if ( ! add_variables(problem,item) )
            {
                free(buffer);
                return false;
            }
        }
        free(buffer);
        return true;
    }
    else
    {
        VARIABLE *item = (VARIABLE*)malloc(sizeof(VARIABLE));
        if ( item == NULL )
        {
            exception("memory allocation error");
        }
        item->spec = strdup(spec);
        if ( item->spec == NULL )
        {
            exception("memory allocation error");
        }
        char *buffer = strdup(spec);
        if ( buffer == NULL )
        {
            exception("memory allocation error");
        }
        char *speclist = buffer;
        item->name = strdup(strsep(&speclist,"="));
        if ( item->name == NULL )
        {
            exception("memory allocation error");
        }
        if ( speclist == NULL )
        {
            exception("variable specification '%s' missing expected '='", buffer);
        }
        item->primal = NULL;
        item->dual = NULL;
        item->columns = item->rows = 0;

        // check for duplicate specification
        for ( VARIABLE *var = problem.variables ; var != NULL ; var = var->next )
        {
            if ( strcmp(var->name,item->name) == 0 )
            {
                exception("variable '%s' already specified, ignoring duplicate definition",item->name);
            }
        }

        // link properties
        char *varspec;
        while (  (varspec=strsep(&speclist,",")) != NULL )
        {
            char classname[65], groupname[65], objectname[65], primalname[65], dualname[65] = "";
            if ( sscanf(varspec,"%[^:]:%[^&]&%[^,]",classname,primalname,dualname) >= 2 )
            {
                add_variable_class(item,classname,primalname,dualname);
            }
            else if ( sscanf(varspec,"%[^@]@%[^/]/%[^,]",groupname,primalname,dualname) >= 2 )
            {
                add_variable_group(item,groupname,primalname,dualname);
            }
            else if ( sscanf(varspec,"%[^.].%[^&]&%[^,]",objectname,primalname,dualname) >= 2 )
            {
                add_variable_object(item,objectname,primalname,dualname);
            }
            else if ( sscanf(varspec,"%[^&]&%[^,]",primalname,dualname) >= 1 )
            {
                add_variable_global(item,primalname,dualname);
            }
            else
            {
                exception("ignoring invalid variable spec '%s'",varspec);
            }
        }

        // link to list
        item->next = problem.variables;
        problem.variables = item;
        free(buffer);
        update_status();
        return true;
    }
}

void cvx::add_primal(VARIABLE *item, gld_property &data)
{
    REFERENCE *ref = new REFERENCE;
    if ( ref == NULL )
    {
        exception("memory allocation error");
    }
    ref->ptr = (double*)data.get_addr();
    ref->next = item->primal;
    item->primal = ref;
}

void cvx::add_dual(VARIABLE *item, gld_property &data)
{
    REFERENCE *ref = new REFERENCE;
    if ( ref == NULL )
    {
        exception("memory allocation error");
    }
    ref->ptr = (double*)data.get_addr();
    ref->next = item->dual;
    item->dual = ref;
}

void cvx::add_variable(VARIABLE *item, OBJECT *obj, const char *primalname,const char *dualname, const char *refname)
{
    gld_property data(obj,primalname);
    if ( ! data.is_valid() )
    {
        exception("primal variable '%s' is not a known property of '%s'",primalname,refname);
    }
    if ( ! data.is_double() )
    {
        exception("primal variable '%s.%s' is not a real number",refname,primalname);
    }
    add_primal(item,data);
    
    if ( strcmp(dualname,"") != 0 )
    {
        gld_property data(obj,dualname);
        if ( ! data.is_valid() )
        {
            exception("dual variable '%s' is not a known property of '%s'",dualname,refname);
        }
        if ( ! data.is_double() )
        {
            exception("dual variable '%s.%s' is not a real number",refname,dualname);
        }
        add_dual(item,data);
    }
    else
    {
        item->dual = NULL;
    }
}

void cvx::add_variable_class(VARIABLE *item, const char *classname, const char *primalname, const char *dualname)
{
    unsigned int count = 0;
    CLASS *oclass = gl_class_get_by_name(classname,NULL);
    for ( OBJECT *obj = gl_object_get_first() ; obj != NULL ; obj = obj->next )
    {
        if ( obj->oclass == oclass )
        {
            add_variable(item,obj,primalname,dualname,classname);
            count++;
        }
    }
    if ( count == 0 )
    {
        warning("class '%s' has no objects",classname);
    }
    if ( item->rows > 0 )
    {
        if ( item->rows != count )
        {
            error("class '%s' rows do not match",classname);
        }
    }
    else
    {
        item->rows = count;
    }
    item->columns++;
}

void cvx::add_variable_group(VARIABLE *item, const char *groupname, const char *primalname, const char *dualname)
{
    unsigned int count = 0;
    for ( OBJECT *obj = gl_object_get_first() ; obj != NULL ; obj = obj->next )
    {
        if ( strcmp(obj->groupid,groupname) == 0 )
        {
            add_variable(item,obj,primalname,dualname,groupname);
            count++;
        }
    }
    if ( count == 0 )
    {
        warning("group '%s' has no objects",groupname);
    }
    if ( item->rows > 0 )
    {
        if ( item->rows != count )
        {
            error("group '%s' rows do not match",groupname);
        }
    }
    else
    {
        item->rows = count;
    }
    item->columns++;
}

void cvx::add_variable_object(VARIABLE *item, const char *objectname, const char *primalname, const char *dualname)
{
    OBJECT *obj = (OBJECT*)get_object(objectname);
    add_variable(item,obj,primalname,dualname,objectname);
    unsigned int count = 1;
    if ( item->rows > 0 )
    {
        if ( item->rows != count )
        {
            error("object '%s' rows do not match",objectname);
        }
    }
    else
    {
        item->rows = count;
    }
    item->columns++;
}

void cvx::add_variable_global(VARIABLE *item, const char *primalname, const char *dualname)
{
    add_variable(item,NULL,primalname,dualname,"global");
    unsigned int count = 1;
    if ( item->rows > 0 )
    {
        if ( item->rows != count )
        {
            error("global '%s' rows do not match",primalname);
        }
    }
    else
    {
        item->rows = count;
    }
    item->columns++;
}

//
// Add a problem constraint
//

bool cvx::add_constraints(struct s_problem &problem, const char *spec)
{
    CONSTRAINT *item = (CONSTRAINT*)malloc(sizeof(CONSTRAINT));
    if ( item == NULL )
    {
        exception("memory allocation error");
        return false;
    }
    item->spec = strdup(spec);
    if ( item->spec == NULL )
    {
        exception("memory allocation error");
        return false;
    }
    item->next = problem.constraints;
    problem.constraints = item;
    return true;
}

//
// Set problem objective
//

bool cvx::set_objective(struct s_problem &problem, const char *spec)
{
    free(problem.objective);
    problem.objective = strdup(spec);
    if ( problem.objective == NULL )
    {
        exception("memory allocation error");
        return false;
    }
    update_status();
    return true;
}

//
// Update status of problem
//
bool cvx::update_status(void)
{
    if ( problem.objective == NULL 
      || strcmp(problem.objective,"") == 0 
      || problem.variables == NULL )
    {
        status = SS_INIT;
        return false;
    }
    status = SS_READY;
    return true;
}

//
// Update solution to problem
//

bool cvx::update_solution(void)
{
    const char *optname = (const char *)get_name();
    bool rc = true;

    verbose("updating solution");
    PyObject *cvx = PyDict_GetItemString(globals,"__cvx__");
    PyObject *objprob = PyDict_GetItemString(cvx,optname);
    PyObject *probdata = PyDict_GetItemString(objprob,"data");

    bool newprob = ( get_event() == OE_INIT );
    if ( probdata == NULL )
    {
        verbose("no existing problem data found for '%s', creating that dict now",optname);
        probdata = PyDict_New();
        PyDict_SetItemString(objprob,"data",probdata);
        newprob = true;
    }

    if ( PyRun_FormatString("__cvx__['%s']['event'] = '%s'",optname,current_event) == -1 )
    {
        status = SS_INVALID;
        exception("objective specification '%s' is invalid",problem.objective);
    }

    // copy data from objects
    Py_ssize_t pos = 0;
    unsigned int n_changed = 0;
    for ( DATA *item = problem.data ; item != NULL ; item = item->next, pos++ )
    {
        bool changed = false;
        Py_ssize_t ndx = 0;
        for ( REFERENCE *ref = item->data ; ref != NULL ; ref=ref->next, ndx++ )
        {
            double value = *(ref->ptr);
            if ( value != PyFloat_AsDouble(PyList_GET_ITEM(item->list,ndx)) )
            {
                changed = true;
                n_changed++;
                if ( newprob || item->is_parameter || ! problem.enable_dpp )
                {
                    PyList_SetItem(item->list,ndx,PyFloat_FromDouble(value));
                }
            }
        }
        if ( ndx != PyList_Size(item->list) )
        {
            status = SS_ERROR;
            exception("%s size mismatch (%d found, %d required)",item->name,ndx,PyList_Size(item->list));
        }

        PyDict_SetItemString(globals,item->name,item->list);
        char dataref[1024];
        char shape[256] = "-1"; // default shape is simple vector
        if ( item->columns > 1 )
        {
            snprintf(shape,sizeof(shape),"(-1,%d)",item->columns);
        }
        snprintf(dataref,sizeof(dataref)-1,"__cvx__['%s']['data']['%s']",optname,item->name);
        if ( changed )
        {
            verbose("data '%s' has changed",item->name);
            if ( problem.enable_dpp )
            {
                verbose("DPP problem (problem.enable_dpp=true for item->name %s --> %s)",item->name,dataref);
                if ( newprob )
                {
                    verbose("new problem requires creation of CVX objects");
                    if ( PyRun_FormatString("%s=np.reshape(np.array(%s),%s)",item->name,item->name,shape) == -1 )
                    {
                        status = SS_ERROR;
                        exception("unable to convert %s to a numpy array",item->name);
                    }    
                    PyDict_SetItemString(probdata,item->name,item->list);
                    if ( item->is_parameter )
                    {
                        verbose("%s is a parameter",item->name);
                        if ( PyRun_FormatString(
                            "%s = %s = Parameter(%s.shape,value=%s,name='%s');"
                            "# DPP parameter creation",
                            dataref, item->name, item->name, item->name, item->name) == -1 )
                        {
                            status = SS_ERROR;
                            exception("unable to define parameter '%s'",item->name);
                        }
                    }
                    else
                    {
                        verbose("%s is a constant",item->name);
                        if ( PyRun_FormatString(
                            "%s = %s = Constant(%s,name='%s');"
                            "# DPP constant constant",
                            dataref, item->name, item->name, item->name) == -1 )
                        {
                            status = SS_ERROR;
                            exception("unable to define constant '%s'",item->name);
                        }
                    }
                }
                else if ( item->is_parameter )
                {
                    verbose("DPP parameter '%s' update",item->name);
                    if ( PyRun_FormatString(
                        "global %s; "
                        "%s.value = %s = np.reshape(np.array(%s),%s);"
                        "# DPP parameter update",
                        item->name,
                        dataref, item->name, item->name, shape) == -1 )
                    {
                        status = SS_ERROR;
                        exception("unable to update parameter '%s'",item->name);
                    }
                }
                else
                {
                    warning("constant '%s' has changed while DPP is enabled",item->name);
                }
            }
            else
            {
                verbose("DCP problem data changed (problem.enable_dpp=false for item->name %s --> %s)\n",item->name,dataref);
                PyDict_SetItemString(probdata,item->name,item->list);
                if ( PyRun_FormatString(
                    "global %s; "
                    "%s = np.reshape(np.array(%s),%s);"
                    "# DCP data update",
                    item->name,
                    item->name, item->name, shape) == -1 )
                {
                    status = SS_ERROR;
                    exception("unable to update data '%s'",item->name);
                }
            }
        }
        else if ( ! problem.enable_dpp )
        {
            verbose("DCP problem data created (problem.enable_dpp=false for item->name %s --> %s)\n",item->name,dataref);
            PyDict_SetItemString(probdata,item->name,item->list);
            if ( PyRun_FormatString(
                "global %s; "
                "%s = np.reshape(np.array(%s),%s);"
                "# DCP data create",
                item->name,
                item->name, item->name, shape) == -1 )
            {
                status = SS_ERROR;
                exception("unable to create data '%s'",item->name);
            }
        }
        else
        {
            verbose("'%s' is unchanged",item->name);
        }
    }
    if ( ! newprob && n_changed == 0 )
    {
        verbose("no data has changed, skipping problem solve and reusing last solution");
        return true;
    }

    if ( newprob || ! problem.enable_dpp )
    {
        // set up variables
        for ( VARIABLE *item = problem.variables ; item != NULL ; item = item->next )
        {
            if ( PyRun_FormatString(
                "%s = Variable(%ld,name='%s')",
                item->name,item->rows,item->name
                ) == -1 )
            {
                status = SS_INVALID;
                exception("unable to create CVX variable '%s'",item->name);
            }
            else
            {
                verbose("variable '%s' created with shape (%ld,%ld)",item->name,item->rows,item->columns);
            }
        }
    }

    // run presolve script
    if ( strcmp(presolve_py,"") != 0 && PyRun_FormatString("%s",presolve_py) == -1 )
    {
        status = SS_ERROR;
        exception("presolve script failed");
    }

    if ( newprob || ! problem.enable_dpp )
    {
        verbose("Constructing objective, constraints, and problem");
        
        // set objective
        if ( PyRun_FormatString("__cvx__['%s']['objective'] = %s",optname,problem.objective) == -1 )
        {
            status = SS_INVALID;
            exception("objective specification '%s' is invalid",problem.objective);
        }

        // update constraints
        char buffer[1024];
        if ( PyRun_FormatString("__cvx__['%s']['constraints'] = [%s]",optname,constraints(buffer,sizeof(buffer)-1)>0?buffer:"") == -1 )
        {
            status = SS_INVALID;
            exception("constraints specification '[%s]' is invalid",buffer);
        }

        // create problem
        if ( PyRun_FormatString("__cvx__['%s']['problem'] = Problem(__cvx__['%s']['objective'],__cvx__['%s']['constraints'])",optname,optname,optname) == -1 )
        {
            if ( strcmp(on_failure,"") != 0 )
            {
                PyRun_FormatString(on_failure);
            }
            status = SS_INVALID;
            exception("problem construction failed");
        }
    }

    // dump problem if requested
    if ( strcmp(problemdump,"") != 0 )
    {
        gld_clock now;
        if ( PyRun_FormatString("print('*** Problem \\'%s\\' at t=%lld (%s) ***\\n',file=__dump__)",optname,gl_globalclock,now.get_string().get_buffer()) == -1
            || PyRun_FormatString("print('Data:',{n:(m.value.tolist() if hasattr(m,'value') else m) for n,m in __cvx__['%s']['data'].items()},file=__dump__)",optname) == -1
            || PyRun_FormatString("print('Objective:',__cvx__['%s']['objective'],file=__dump__)",optname) == -1
            || PyRun_FormatString("print('Contraints:',__cvx__['%s']['constraints'],file=__dump__)",optname) == -1 )
        {
            exception("problem dump preamble failed");
        }

    }

    // solve problem
    if ( PyRun_FormatString(
        "try: __cvx__['%s']['value'],__cvx__['%s']['result'] = __cvx__['%s']['problem'].solve(%s),None\n"
        "except Exception as err: __cvx__['%s']['value'],__cvx__['%s']['result'] = None,{'status':'FAILED','error':err}",
        optname,optname,optname,(const char*)solver_options,
        optname,optname) == -1 )
    {
        if ( strcmp(problemdump,"") != 0 )
        {
            PyRun_FormatString("print('Problem rejected',file=__dump__,flush=True)");
        }
        if ( strcmp(on_exception,"") != 0 )
        {
            PyRun_FormatString(on_exception);
        }
        status = SS_FAILED;
        switch (failure_handling)
        {
        case OF_HALT:
            error("problem failed");
            break;
        case OF_WARN:
            warning("problem failed");
            break;
        case OF_IGNORE:
            verbose("problem failed");
            break;
        default:
            status = SS_ERROR;
            exception("invalid handling (failure_handling=%ld) for %s problem",failure_handling,this->value>0?"infeasible":"unbounded");
            break;
        }
    }

    // dump problem
    if ( strcmp(problemdump,"") != 0 && 
        PyRun_FormatString("print('\\n'.join([f'{x}: {y}' for x,y in __cvx__['%s']['problem'].get_problem_data(None)[0].items()]),file=__dump__)",optname,optname) == -1 )
    {
        status = SS_ERROR;
        exception("problem dump failed");
    }

    // extract value, if any
    PyObject *value = PyDict_GetItemString(objprob,"value");
    PyObject *result = PyDict_GetItemString(objprob,"result");

    if ( result == Py_None )
    {
        this->value = PyFloat_AS_DOUBLE(value);
    }
    else
    {
        this->value = QNAN;
    }
    if ( ! isfinite(this->value) )
    {
        const char *statusmsg = "unknown status";
        if ( this->value < 0 )
        {
            status = SS_UNBOUNDED;
            statusmsg = "unbounded";
            if ( strcmp(problemdump,"") != 0 )
            {
                PyRun_FormatString("print('Problem is unbounded\\n',file=__dump__,flush=True)");
            }
            if ( strcmp(on_unbounded,"") != 0 )
            {
                PyRun_FormatString(on_unbounded);
            }
        }
        else if ( this->value > 0 )
        {
            status = SS_INFEASIBLE;
            statusmsg = "infeasible";
            if ( strcmp(problemdump,"") != 0 )
            {
                PyRun_FormatString("print('Problem is infeasible\\n',file=__dump__,flush=True)");
            }
            if ( strcmp(on_infeasible,"") != 0 )
            {
                PyRun_FormatString(on_infeasible);
            }
        }
        else
        {
            status = SS_FAILED;
            statusmsg = "failed";
            if ( strcmp(problemdump,"") != 0 )
            {
                PyRun_FormatString("print('Problem solution failed (\\n',__cvx__['%s']['result'],'\\n',file=__dump__,flush=True)",optname);
            }
            if ( strcmp(on_failure,"") != 0 )
            {
                PyRun_FormatString(on_failure);
            }            
        }    
        switch (failure_handling)
        {
        case OF_HALT:
            error("problem %s",statusmsg);
            break;
        case OF_WARN:
            warning("problem %s",statusmsg);
            break;
        case OF_IGNORE:
            verbose("ignoring %s problem",statusmsg);
            break;
        default:
            status = SS_ERROR;
            exception("invalid handling (failure_handling=%ld) for %s problem",failure_handling,statusmsg);
            break;
        }
        rc = false;
    }
    else
    {
        if ( strcmp(problemdump,"") != 0 )
        {
            PyRun_FormatString("print('Problem solved: objective value is',__cvx__['%s']['problem'].value,file=__dump__,flush=True)",optname);
        }

        char buffer[65536];
        const char *status_string[] = {
            "INIT",
            "READY",
            "OPTIMAL",
            "INACCURATE",
            "INFEASIBLE",
            "UNBOUNDED",
            "INVALID",
            "ERROR",
            "FAILED",
        };
        // TODO: replace this with a direct call to value.tolist()
        int len = snprintf(buffer,sizeof(buffer)-1,"__cvx__['%s']['result'] = {'status':'%s',",optname,status_string[status]);
        for ( VARIABLE *item = problem.variables ; item != NULL ; item = item->next )
        {
            len += snprintf(buffer+len,sizeof(buffer)-len-1,"'%s':%s.value.round(12).tolist()[-1::-1] if not %s.value is None else [],",
                item->name,item->name,item->name);
        }
        len += snprintf(buffer+len,sizeof(buffer)-len-1,"%s","}\n");
        if ( PyRun_FormatString(buffer) < 0 )
        {
            status = SS_ERROR;
            exception("unable to get result");
        }

        if ( strcmp(postsolve_py,"") != 0 &&  PyRun_FormatString("%s",postsolve_py) == -1 )
        {
            status = SS_ERROR;
            exception("postsolve script failed");
        }

        PyObject *result = PyDict_GetItemString(PyDict_GetItemString(cvx,optname),"result");
        for ( VARIABLE *item = problem.variables ; item != NULL ; item = item->next )
        {
            Py_ssize_t ndx = 0;
            PyObject *var = PyDict_GetItemString(result,item->name);
            for ( REFERENCE *prop = item->primal ; prop != NULL ; prop = prop->next, ndx++ )
            {
                *(prop->ptr) = PyFloat_AS_DOUBLE(PyList_GET_ITEM(var,ndx));
            }
            for ( REFERENCE *prop = item->dual ; prop != NULL ; prop = prop->next, ndx++ )
            {
                *(prop->ptr) = PyFloat_AS_DOUBLE(PyList_GET_ITEM(var,ndx));
            }
        }
        if ( strcmp(problemdump,"") != 0 )
        {
            PyRun_FormatString("print('Result:',__cvx__['%s']['result'],'\\n',file=__dump__,flush=True)",optname);
        }
        status = SS_OPTIMAL;
        rc = true;
    }

    return rc;
}

