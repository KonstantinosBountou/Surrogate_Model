import pandas as pd
from sklearn.preprocessing import FunctionTransformer, OneHotEncoder
from sklearn.compose import ColumnTransformer
from sklearn.impute import SimpleImputer
from sklearn.pipeline import Pipeline
from sklearn.multioutput import MultiOutputRegressor
from sklearn.ensemble import ExtraTreesRegressor
from compare_tdd_features import structural

def features(df, numeric, categorical):
    x=df[numeric+categorical].copy()
    for c in numeric:x[c]=pd.to_numeric(x[c],errors='raise')
    for c in categorical:x[c]=x[c].astype(str)
    extra=pd.DataFrame([structural(p,mu) for p,mu in zip(x.tdd_pattern,x.numerology)],index=x.index)
    return pd.concat([x,extra],axis=1)

def build(schema):
    nums,cats=schema['numeric'],schema['categorical']
    pre=ColumnTransformer([
        ('num',SimpleImputer(strategy='median',add_indicator=True,keep_empty_features=True),nums+list(structural('DL|UL|',0))),
        ('cat',OneHotEncoder(handle_unknown='ignore',sparse_output=False),cats)])
    return Pipeline([('features',FunctionTransformer(features,kw_args=dict(numeric=nums,categorical=cats),validate=False)),
                     ('preprocess',pre),('model',MultiOutputRegressor(ExtraTreesRegressor(n_estimators=160,min_samples_leaf=1,max_features=1.0,random_state=42,n_jobs=2)))])
