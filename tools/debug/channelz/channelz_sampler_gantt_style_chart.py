import plotly as py

import plotly.figure_factory as ff

import json

import sys

if __name__ == '__main__':

  if (len(sys.argv) != 2):
    print('Please pass the input json file!')
    sys.exit(1)

  pyplt = py.offline.plot

  # Read Json data
  df = []
  with open(sys.argv[1], 'r') as f:
    temp = json.loads(f.read())

  for i in temp:
    df.append(
        dict(
            Task=i['Task'],
            Start=i['Start'],
            Finish=i['Finish'],
            ID=i['ID'],
            Type=i['Type'],
            Description=i['Description']))
  sorted_df = sorted(df, key=lambda x: (x['Task'], x['Start']))

  # Create Gantt style chart
  colors = {
      'Channel': 'rgb(220, 0, 0)',
      'Subchannel': 'rgb(0, 220, 0)',
      'Socket': 'rgb(0, 0, 220)',
      'Server': 'rgb(210, 60, 180)'
  }
  fig = ff.create_gantt(
      sorted_df,
      colors=colors,
      index_col='Type',
      title='channelz_sampler',
      bar_width=0.12,
      showgrid_x=True,
      show_colorbar=True,
      group_tasks=False,
      show_hover_fill=True)
  pyplt(fig, filename='gantt.html')

