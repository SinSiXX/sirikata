
if (typeof(std) == 'undefined')
    std = {};

if (typeof(std.simpleStorage) != 'undefined')
    throw 'Error in simple storage.  Should only import once.';


(function()
{
    system.require('std/shim/restore/persistService.em');
    std.simpleStorage = {  };

    var charKeyName   =    'pCharsKeyName';
    var scriptKeyName =   'pScriptKeyName';


    var mChars = { };
    var mScript = {  };
    
    std.simpleStorage.setField = function(fieldName,fieldVal)
    {
        mChars[fieldName] = fieldVal;
        std.persist.checkpointPartialPersist(mChars,charKeyName);
    };

    
    
    std.simpleStorage.readField = function(fieldName,cb,def)
    {
        std.persist.restoreFromAsync(charKeyName,
                                     function(success,val)
                                     {
                                         if ((success) && (fieldName in val))
                                             cb(val.fieldName);
                                         else
                                             cb(def);
                                     });
    };


    std.simpleStorage.setScript = function(newScriptFunc,executeOnSet)
    {
        var newScript =  ("(" + newScriptFunc + ")();");
        if (typeof(executeOnSet) === 'undefined')
            executeOnSet = false;
        
        mScript.script = newScript;

        
        var cbFunc = function(){ };
        if (executeOnSet)
            cbFunc = function(){ system.eval(newScript);  };

        std.persist.checkpointPartialPersist(mScript,scriptKeyName,cbFunc);
    };

    
    std.simpleStorage.readScript = function(cb,def)
    {
        std.persist.restoreFromAsync(scriptKeyName,
                                     function(success,val)
                                     {
                                         system.print('\nDEBUG: in read script callback\n');
                                         system.print(success);
                                         system.print('\n');
                                         if ((success) && ('script' in val))
                                             cb(val.script);
                                         else
                                             cb(def);
                                     });
    };


    function onRestore(scriptToEval)
    {
        system.eval(scriptToEval);
    };
    //lkjs;
    std.simpleStorage.readScript(onRestore,"");
    
})();

